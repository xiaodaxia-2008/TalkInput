#pragma once

#include "offline_speech_recognizer.h"

#include <string>

namespace talkinput
{

class Qwen3ASRSpeechRecognizer final : public OfflineSpeechRecognizer
{
public:
    explicit Qwen3ASRSpeechRecognizer(QObject *parent = nullptr)
        : OfflineSpeechRecognizer(parent)
    {
    }

protected:
    bool configureModel(const Config &config,
                        SherpaOnnxOfflineRecognizerConfig *recognizer,
                        QString *errorMessage) override;

private:
    std::string m_convFrontendPath;
    std::string m_encoderPath;
    std::string m_decoderPath;
    std::string m_tokenizerDir;
    std::string m_hotwords;
};

} // namespace talkinput
