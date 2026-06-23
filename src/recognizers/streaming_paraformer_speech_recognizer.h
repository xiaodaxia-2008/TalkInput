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
    std::expected<void, QString>
    configureModel(SherpaOnnxOnlineRecognizerConfig *recognizer) override;

    bool supportsModifiedBeamSearch() const override
    {
        return false;
    }
};

} // namespace talkinput
