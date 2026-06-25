#pragma once

#include "speech_recognizer.h"

struct SherpaOnnxOnlineRecognizer;
struct SherpaOnnxOnlineRecognizerConfig;
struct SherpaOnnxOnlineStream;

namespace talkinput
{

class OnlineSpeechRecognizer : public SpeechRecognizer
{
public:
    explicit OnlineSpeechRecognizer(QObject *parent = nullptr);
    ~OnlineSpeechRecognizer() override;

    std::expected<void, QString> start() final;
    void stop() override;
    bool isRunning() const final;
    bool isStreaming() const final;

    void acceptPcm16(const QByteArray &audioData, int sampleRate,
                     int channelCount) final;
    void finish() final;
    void resetStream() final;

protected:
    virtual std::expected<void, QString>
    configureModel(SherpaOnnxOnlineRecognizerConfig *recognizer) = 0;
    virtual bool supportsModifiedBeamSearch() const;

private:
    void decodePending();
    void publishResult(bool isFinal);

    const SherpaOnnxOnlineRecognizer *m_recognizer = nullptr;
    const SherpaOnnxOnlineStream *m_stream = nullptr;
    QString m_lastText;
};

} // namespace talkinput
