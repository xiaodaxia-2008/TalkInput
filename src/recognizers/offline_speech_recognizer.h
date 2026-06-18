#pragma once

#include "speech_recognizer.h"

#include <string>
#include <vector>

struct SherpaOnnxOfflineRecognizer;
struct SherpaOnnxOfflineRecognizerConfig;

namespace talkinput
{

class OfflineSpeechRecognizer : public SpeechRecognizer
{
public:
    explicit OfflineSpeechRecognizer(QObject *parent = nullptr);
    ~OfflineSpeechRecognizer() override;

    bool start(const nlohmann::json &config, QString *errorMessage) final;
    void stop() override;
    bool isRunning() const final;
    bool isStreaming() const final;

    void acceptPcm16(const QByteArray &audioData, int sampleRate,
                     int channelCount) final;
    void finish() final;
    void resetStream() final;

protected:
    virtual bool configureModel(const nlohmann::json &config,
                                SherpaOnnxOfflineRecognizerConfig *recognizer,
                                QString *errorMessage) = 0;
    virtual int chunkSeconds() const;

    std::string m_tokensPath;
    std::string m_modelingUnit;

private:
    void decode();

    const SherpaOnnxOfflineRecognizer *m_recognizer = nullptr;
    std::vector<float> m_samples;
    int m_modelSampleRate = 16000;
    int m_inputSampleRate = 0;
};

} // namespace talkinput
