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
    std::expected<void, QString>
    configureModel(SherpaOnnxOfflineRecognizerConfig *recognizer) override;

private:
    std::string m_modelPath;
    std::string m_language;
};

} // namespace talkinput
