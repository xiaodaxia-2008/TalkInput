#include "system_speech_recognizer.h"
#include "logging.h"

namespace talkinput
{

// ---------------------------------------------------------------------------
// Platform-specific implementation — macOS stub (not yet implemented)
// ---------------------------------------------------------------------------
class SystemSpeechRecognizer::Impl
{
public:
    Impl() = default;

    ~Impl()
    {
        stop();
    }

    bool start(const Config &, QString *errorMessage,
               QPointer<SystemSpeechRecognizer>)
    {
        SPDLOG_WARN("System speech recognizer is not implemented on macOS");
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "System speech recognition is not available on this platform.");
        }
        return false;
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

bool SystemSpeechRecognizer::start(const Config &config, QString *errorMessage)
{
    return m_impl->start(config, errorMessage, this);
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
