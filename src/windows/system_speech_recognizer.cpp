#include "system_speech_recognizer.h"
#include "logging.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Media.SpeechRecognition.h>
#include <winrt/base.h>

#include <QMetaObject>
#include <QPointer>
#include <memory>

// WinRT namespace aliases
namespace Speech = winrt::Windows::Media::SpeechRecognition;

namespace
{

void initWinrtApartment()
{
    thread_local bool initialized = false;
    if (initialized) {
        return;
    }

    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    catch (const winrt::hresult_error &e) {
        if (e.code() != RPC_E_CHANGED_MODE) {
            throw;
        }
    }
    initialized = true;
}

QString windowsLanguageTag(const QString &language)
{
    if (language == QStringLiteral("zh")) {
        return QStringLiteral("zh-CN");
    }
    if (language == QStringLiteral("en")) {
        return QStringLiteral("en-US");
    }
    return language;
}

} // namespace

namespace talkinput
{

// ---------------------------------------------------------------------------
// Platform-specific implementation
// ---------------------------------------------------------------------------
class SystemSpeechRecognizer::Impl
{
public:
    Impl() = default;

    ~Impl()
    {
        stop();
    }

    bool start(const nlohmann::json &config, QString *errorMessage,
               QPointer<SystemSpeechRecognizer> context)
    {
        try {
            initWinrtApartment();

            // Create recognizer
            const nlohmann::json params =
                config.value("params", nlohmann::json::object());
            const QString language =
                windowsLanguageTag(jsonString(params, "language", "zh"));
            if (!language.isEmpty()) {
                m_recognizer = Speech::SpeechRecognizer(
                    winrt::Windows::Globalization::Language(
                        language.toStdWString()));
                SPDLOG_DEBUG("System recognizer language: {}", language);
            }
            else {
                m_recognizer = Speech::SpeechRecognizer();
            }

            // Compile the default (dictation) grammar
            const auto compilationResult =
                m_recognizer.CompileConstraintsAsync().get();
            if (compilationResult.Status() !=
                Speech::SpeechRecognitionResultStatus::Success)
            {
                if (errorMessage) {
                    *errorMessage =
                        QStringLiteral("Speech grammar compilation failed");
                }
                SPDLOG_WARN("System recognizer: grammar compilation failed");
                return false;
            }

            // Hook HypothesisGenerated event on the recognizer (partial
            // results)
            m_hypothesisToken = m_recognizer.HypothesisGenerated(
                [ctx = context](auto &&, auto &&args) {
                    const QString text =
                        QString::fromStdWString(
                            std::wstring(args.Hypothesis().Text()))
                            .trimmed();
                    if (text.isEmpty()) {
                        return;
                    }
                    SPDLOG_DEBUG("System recognizer hypothesis: {}", text);
                    if (ctx) {
                        QMetaObject::invokeMethod(ctx, [=]() {
                            if (ctx) {
                                emit ctx->resultChanged(text, false);
                            }
                        });
                    }
                });

            // Hook ResultGenerated on the continuous session (final results)
            auto session = m_recognizer.ContinuousRecognitionSession();
            m_resultToken =
                session.ResultGenerated([ctx = context](auto &&, auto &&args) {
                    const auto result = args.Result();
                    const QString text =
                        QString::fromStdWString(std::wstring(result.Text()))
                            .trimmed();
                    if (text.isEmpty()) {
                        return;
                    }
                    const bool isFinal =
                        result.Status() ==
                        Speech::SpeechRecognitionResultStatus::Success;
                    SPDLOG_DEBUG("System recognizer {}: {}",
                                 isFinal ? "final" : "intermediate", text);
                    if (ctx) {
                        QMetaObject::invokeMethod(ctx, [=]() {
                            if (ctx) {
                                emit ctx->resultChanged(text, isFinal);
                            }
                        });
                    }
                });

            // Start continuous recognition
            session.StartAsync().get();
            m_running = true;

            SPDLOG_INFO("System recognizer started");
            return true;
        }
        catch (const winrt::hresult_error &e) {
            const QString msg = QString::fromStdWString(e.message().c_str());
            SPDLOG_WARN("System recognizer start failed: {}", msg);
            if (errorMessage) {
                *errorMessage = msg;
            }
            return false;
        }
    }

    void stop()
    {
        if (!m_running) {
            return;
        }

        try {
            if (m_recognizer) {
                if (m_resultToken) {
                    auto session = m_recognizer.ContinuousRecognitionSession();
                    session.ResultGenerated(m_resultToken);
                    session.StopAsync().get();
                    m_resultToken = {};
                }
                if (m_hypothesisToken) {
                    m_recognizer.HypothesisGenerated(m_hypothesisToken);
                    m_hypothesisToken = {};
                }
            }
        }
        catch (const winrt::hresult_error &e) {
            SPDLOG_WARN("System recognizer stop: {}",
                        winrt::to_string(e.message()));
        }

        m_running = false;
        SPDLOG_INFO("System recognizer stopped");
    }

    bool isRunning() const
    {
        return m_running;
    }

    bool isStreaming() const
    {
        return m_running;
    }

private:
    Speech::SpeechRecognizer m_recognizer = nullptr;
    winrt::event_token m_resultToken;
    winrt::event_token m_hypothesisToken;
    bool m_running = false;
};

// ---------------------------------------------------------------------------
// SystemSpeechRecognizer public API
// ---------------------------------------------------------------------------
SystemSpeechRecognizer::SystemSpeechRecognizer(QObject *parent)
    : SpeechRecognizer(parent), m_impl(std::make_unique<Impl>())
{
}

SystemSpeechRecognizer::~SystemSpeechRecognizer() = default;

bool SystemSpeechRecognizer::start(const nlohmann::json &config,
                                   QString *errorMessage)
{
    Q_UNUSED(errorMessage);
    m_config = config;
    return true;
}

void SystemSpeechRecognizer::stop()
{
    m_impl->stop();
}

bool SystemSpeechRecognizer::isRunning() const
{
    return m_impl->isRunning();
}

bool SystemSpeechRecognizer::isStreaming() const
{
    return true;
}

void SystemSpeechRecognizer::acceptPcm16(const QByteArray &, int, int)
{
    // System recognizer uses its own microphone; PCM16 is ignored.
}

void SystemSpeechRecognizer::finish()
{
    m_impl->stop();
}

void SystemSpeechRecognizer::resetStream()
{
    if (m_impl->isRunning()) {
        return;
    }

    QString error;
    if (!m_impl->start(m_config, &error, this)) {
        SPDLOG_WARN("System recognizer session start failed: {}", error);
    }
}

bool SystemSpeechRecognizer::acceptsExternalAudio() const
{
    return false;
}

} // namespace talkinput
