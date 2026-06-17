#pragma once

#include "online_speech_recognizer.h"

namespace talkinput
{

class StreamingTransducerRecognizer final : public OnlineSpeechRecognizer
{
public:
    explicit StreamingTransducerRecognizer(QObject *parent = nullptr)
        : OnlineSpeechRecognizer(parent)
    {
    }

protected:
    bool configureModel(const Config &config,
                        SherpaOnnxOnlineRecognizerConfig *recognizer,
                        QString *errorMessage) override;
};

} // namespace talkinput
