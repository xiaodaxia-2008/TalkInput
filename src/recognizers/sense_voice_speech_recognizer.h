#pragma once

#include "offline_speech_recognizer.h"

namespace zenny
{

class SenseVoiceSpeechRecognizer final : public OfflineSpeechRecognizer
{
public:
    explicit SenseVoiceSpeechRecognizer(QObject *parent = nullptr)
        : OfflineSpeechRecognizer(parent, 18)
    {
    }

protected:
    std::expected<void, QString>
    configureModel(SherpaOnnxOfflineRecognizerConfig *recognizer) override;
};

} // namespace zenny
