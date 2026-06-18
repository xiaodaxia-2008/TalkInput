#pragma once

#include "online_speech_recognizer.h"

namespace talkinput
{

class StreamingParaformerSpeechRecognizer final : public OnlineSpeechRecognizer
{
public:
    explicit StreamingParaformerSpeechRecognizer(QObject *parent = nullptr)
        : OnlineSpeechRecognizer(parent)
    {
    }

protected:
    bool configureModel(const Config &config,
                        SherpaOnnxOnlineRecognizerConfig *recognizer,
                        QString *errorMessage) override;
};

} // namespace talkinput
