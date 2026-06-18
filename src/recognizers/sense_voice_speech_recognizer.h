#pragma once

#include "offline_speech_recognizer.h"

#include <string>

namespace talkinput
{

class SenseVoiceSpeechRecognizer final : public OfflineSpeechRecognizer
{
public:
    explicit SenseVoiceSpeechRecognizer(QObject *parent = nullptr)
        : OfflineSpeechRecognizer(parent)
    {
    }

protected:
    bool configureModel(const nlohmann::json &config,
                        SherpaOnnxOfflineRecognizerConfig *recognizer,
                        QString *errorMessage) override;

private:
    std::string m_modelPath;
    std::string m_language;
};

} // namespace talkinput
