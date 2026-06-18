#pragma once

#include "online_speech_recognizer.h"

namespace talkinput
{

class StreamingTransducerSpeechRecognizer final : public OnlineSpeechRecognizer
{
public:
    explicit StreamingTransducerSpeechRecognizer(QObject *parent = nullptr)
        : OnlineSpeechRecognizer(parent)
    {
    }

protected:
    bool configureModel(const Config &config,
                        SherpaOnnxOnlineRecognizerConfig *recognizer,
                        QString *errorMessage) override;
};

} // namespace talkinput
