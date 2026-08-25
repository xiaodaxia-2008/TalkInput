#pragma once

#include "offline_speech_recognizer.h"

namespace zenny
{

class FunASRNanoSpeechRecognizer final : public OfflineSpeechRecognizer
{
public:
    explicit FunASRNanoSpeechRecognizer(QObject *parent = nullptr)
        : OfflineSpeechRecognizer(parent, 18)
    {
    }

protected:
    std::expected<void, QString>
    configureModel(SherpaOnnxOfflineRecognizerConfig *recognizer) override;
    QString normalizeResultText(const QString &text) const override;
};

} // namespace zenny
