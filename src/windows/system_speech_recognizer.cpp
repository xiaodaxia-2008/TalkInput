#include "../system_speech_recognizer.h"
#include "../logging.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Media.SpeechRecognition.h>
#include <winrt/base.h>

#include <QMetaObject>
#include <QPointer>
#include <chrono>
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

    // Qt initializes the main thread as STA. Try STA first so
    // winrt::init_apartment can complete its internal initialization
    // instead of throwing RPC_E_CHANGED_MODE and leaving WinRT uninitialised.
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    }
    catch (const winrt::hresult_error &e) {
        if (e.code() != RPC_E_CHANGED_MODE) {
            throw;
        }
        // Thread was initialised as MTA (e.g. background worker);
        // match the apartment type.
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    initialized = true;
}

QString windowsLanguageTag(const QString &language)
{
    if (language.isEmpty() || language == QStringLiteral("system")) {
        return {};
    }
    if (language == QStringLiteral("zh")) {
        return QStringLiteral("zh-CN");
    }
    if (language == QStringLiteral("en")) {
        return QStringLiteral("en-US");
    }
    return language;
}

QString completedStatusName(Speech::SpeechRecognitionResultStatus status)
{
    switch (status) {
    case Speech::SpeechRecognitionResultStatus::Success:
        return QStringLiteral("Success");
    case Speech::SpeechRecognitionResultStatus::TopicLanguageNotSupported:
        return QStringLiteral("TopicLanguageNotSupported");
    case Speech::SpeechRecognitionResultStatus::GrammarLanguageMismatch:
        return QStringLiteral("GrammarLanguageMismatch");
    case Speech::SpeechRecognitionResultStatus::GrammarCompilationFailure:
        return QStringLiteral("GrammarCompilationFailure");
    case Speech::SpeechRecognitionResultStatus::AudioQualityFailure:
        return QStringLiteral("AudioQualityFailure");
    case Speech::SpeechRecognitionResultStatus::UserCanceled:
        return QStringLiteral("UserCanceled");
    case Speech::SpeechRecognitionResultStatus::TimeoutExceeded:
        return QStringLiteral("TimeoutExceeded");
    case Speech::SpeechRecognitionResultStatus::PauseLimitExceeded:
        return QStringLiteral("PauseLimitExceeded");
    case Speech::SpeechRecognitionResultStatus::NetworkFailure:
        return QStringLiteral("NetworkFailure");
    case Speech::SpeechRecognitionResultStatus::MicrophoneUnavailable:
        return QStringLiteral("MicrophoneUnavailable");
    case Speech::SpeechRecognitionResultStatus::Unknown:
    default:
        return QStringLiteral("Unknown");
    }
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

    std::expected<void, QString> start(const nlohmann::json &config,
                                       QPointer<SystemSpeechRecognizer> context)
    {
        stop();

        try {
            initWinrtApartment();

            // Create recognizer
            const nlohmann::json params =
                config.value("params", nlohmann::json::object());
            const QString language =
                windowsLanguageTag(jsonString(params, "language", "system"));
            if (!language.isEmpty()) {
                m_recognizer = Speech::SpeechRecognizer(
                    winrt::Windows::Globalization::Language(
                        language.toStdWString()));
                SPDLOG_DEBUG("System recognizer language: {}", language);
            }
            else {
                m_recognizer = Speech::SpeechRecognizer();
            }

            m_recognizer.Constraints().Clear();
            m_recognizer.Constraints().Append(
                Speech::SpeechRecognitionTopicConstraint(
                    Speech::SpeechRecognitionScenario::Dictation,
                    L"dictation"));
            m_recognizer.Timeouts().InitialSilenceTimeout(
                std::chrono::seconds(30));
            m_recognizer.Timeouts().EndSilenceTimeout(std::chrono::seconds(2));

            const auto compilationResult =
                m_recognizer.CompileConstraintsAsync().get();
            if (compilationResult.Status() !=
                Speech::SpeechRecognitionResultStatus::Success)
            {
                SPDLOG_WARN("System recognizer: grammar compilation failed");
                return std::unexpected(
                    QStringLiteral("Speech grammar compilation failed"));
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

            // Register Completed before StartAsync so we never miss the
            // session-completion event — even if it fires immediately.
            m_completedToken = session.Completed([this](auto &&, auto &&args) {
                m_running = false;
                const QString status = completedStatusName(args.Status());
                if (m_stopRequested ||
                    args.Status() ==
                        Speech::SpeechRecognitionResultStatus::Success)
                {
                    SPDLOG_DEBUG("System recognizer completed: {}", status);
                }
                else {
                    SPDLOG_WARN("System recognizer completed: {}", status);
                }
            });

            // Start continuous recognition
            m_stopRequested = false;
            session.StartAsync().get();
            m_running = true;

            SPDLOG_INFO("System recognizer started");
            return {};
        }
        catch (const winrt::hresult_error &e) {
            const QString msg = QString::fromStdWString(e.message().c_str());
            SPDLOG_WARN("System recognizer start failed: {}", msg);
            return std::unexpected(msg);
        }
    }

    void stop()
    {
        if (!m_recognizer) {
            m_running = false;
            return;
        }

        const bool wasRunning = m_running;
        m_stopRequested = true;
        try {
            auto session = m_recognizer.ContinuousRecognitionSession();
            if (m_completedToken) {
                session.Completed(m_completedToken);
                m_completedToken = {};
            }
            if (m_resultToken) {
                session.ResultGenerated(m_resultToken);
                m_resultToken = {};
            }
            if (m_hypothesisToken) {
                m_recognizer.HypothesisGenerated(m_hypothesisToken);
                m_hypothesisToken = {};
            }
            if (wasRunning) {
                session.StopAsync().get();
            }
        }
        catch (const winrt::hresult_error &e) {
            SPDLOG_WARN("System recognizer stop: {}",
                        winrt::to_string(e.message()));
        }

        m_recognizer = nullptr;
        m_running = false;
        m_stopRequested = false;
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
    winrt::event_token m_completedToken;
    bool m_running = false;
    bool m_stopRequested = false;
};

// ---------------------------------------------------------------------------
// SystemSpeechRecognizer public API
// ---------------------------------------------------------------------------
SystemSpeechRecognizer::SystemSpeechRecognizer(QObject *parent)
    : SpeechRecognizer(parent), m_impl(std::make_unique<Impl>())
{
}

SystemSpeechRecognizer::~SystemSpeechRecognizer() = default;

std::expected<void, QString>
SystemSpeechRecognizer::start(const nlohmann::json &config)
{
    m_config = config;
    return {};
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

    auto startResult = m_impl->start(m_config, this);
    if (!startResult) {
        SPDLOG_WARN("System recognizer session start failed: {}",
                    startResult.error());
        STATUSBAR_ERROR("{}", tr("System speech recognition failed: %1")
                                  .arg(startResult.error()));
    }
}

bool SystemSpeechRecognizer::acceptsExternalAudio() const
{
    return false;
}

} // namespace talkinput
