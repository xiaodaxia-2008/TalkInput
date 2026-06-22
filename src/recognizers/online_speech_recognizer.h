#pragma once

#include "speech_recognizer.h"

#include <string>
#include <vector>

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

    std::expected<void, QString> start(const AsrPreset &preset) final;
    void stop() override;
    bool isRunning() const final;
    bool isStreaming() const final;

    void acceptPcm16(const QByteArray &audioData, int sampleRate,
                     int channelCount) final;
    void finish() final;
    void resetStream() final;

protected:
    virtual std::expected<void, QString>
    configureModel(const AsrPreset &preset,
                   SherpaOnnxOnlineRecognizerConfig *recognizer) = 0;
    virtual bool supportsModifiedBeamSearch() const;

    std::string m_encoderPath;
    std::string m_decoderPath;
    std::string m_joinerPath;
    std::string m_tokensPath;

private:
    void decodePending();
    void publishResult(bool isFinal);

    const SherpaOnnxOnlineRecognizer *m_recognizer = nullptr;
    const SherpaOnnxOnlineStream *m_stream = nullptr;
    QString m_lastText;
    std::string m_modelingUnit;
    std::string m_hotwordsText;
};

} // namespace talkinput
