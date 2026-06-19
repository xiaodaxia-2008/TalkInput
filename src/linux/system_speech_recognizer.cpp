#include "../system_speech_recognizer.h"
#include "../logging.h"

namespace talkinput
{

// ---------------------------------------------------------------------------
// Platform-specific implementation — Linux stub (not yet implemented)
// ---------------------------------------------------------------------------
class SystemSpeechRecognizer::Impl
{
public:
    Impl() = default;

    ~Impl()
    {
        stop();
    }

    std::expected<void, QString> start(const nlohmann::json &,
                                        QPointer<SystemSpeechRecognizer>)
    {
        SPDLOG_WARN("System speech recognizer is not implemented on Linux");
        return std::unexpected(QStringLiteral(
            "System speech recognition is not available on this platform."));
    }

    void stop()
    {
    }

    bool isRunning() const
    {
        return false;
    }

    bool isStreaming() const
    {
        return false;
    }

private:
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

std::expected<void, QString>
SystemSpeechRecognizer::start(const nlohmann::json &config)
{
    return m_impl->start(config, this);
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
    return m_impl->isStreaming();
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
    // No-op for system recognizer.
}

bool SystemSpeechRecognizer::acceptsExternalAudio() const
{
    return false;
}

} // namespace talkinput
