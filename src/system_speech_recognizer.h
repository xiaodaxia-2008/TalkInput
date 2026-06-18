#pragma once

#include "speech_recognizer.h"

namespace talkinput
{

/// Speech recognizer that uses the operating system's native speech
/// recognition service (e.g. Windows.Media.SpeechRecognition on Windows,
/// NSSpeechRecognizer on macOS, Speech API on Linux).
///
/// On platforms where system speech recognition is unavailable,
/// isRunning() returns false and start() returns false.
class SystemSpeechRecognizer final : public SpeechRecognizer
{
    Q_OBJECT

public:
    explicit SystemSpeechRecognizer(QObject *parent = nullptr);
    ~SystemSpeechRecognizer() override;

    bool start(const Config &config, QString *errorMessage) override;
    void stop() override;
    bool isRunning() const override;
    bool isStreaming() const override;

    // System recognizer uses its own microphone; PCM16 input is ignored.
    void acceptPcm16(const QByteArray &audioData, int sampleRate,
                     int channelCount) override;
    void finish() override;
    void resetStream() override;
    bool acceptsExternalAudio() const override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    Config m_config;
};

} // namespace talkinput
