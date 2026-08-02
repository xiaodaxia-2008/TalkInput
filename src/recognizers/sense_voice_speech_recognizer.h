#pragma once

#include "offline_speech_recognizer.h"

namespace talkinput
{

class SenseVoiceSpeechRecognizer final : public OfflineSpeechRecognizer
{
public:
    explicit SenseVoiceSpeechRecognizer(QObject *parent = nullptr)
        : OfflineSpeechRecognizer(parent, 10, 30)
    {
    }

protected:
    std::expected<void, QString>
    configureModel(SherpaOnnxOfflineRecognizerConfig *recognizer) override;
};

} // namespace talkinput
