#pragma once

#include "online_speech_recognizer.h"

namespace talkinput
{

class StreamingParaformerRecognizer final : public OnlineSpeechRecognizer
{
public:
    explicit StreamingParaformerRecognizer(QObject *parent = nullptr)
        : OnlineSpeechRecognizer(parent)
    {
    }

protected:
    bool configureModel(const Config &config,
                        SherpaOnnxOnlineRecognizerConfig *recognizer,
                        QString *errorMessage) override;
};

} // namespace talkinput
