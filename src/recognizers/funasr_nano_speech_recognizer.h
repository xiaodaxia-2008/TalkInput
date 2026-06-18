#pragma once

#include "offline_speech_recognizer.h"

#include <string>

namespace talkinput
{

class FunASRNanoSpeechRecognizer final : public OfflineSpeechRecognizer
{
public:
    explicit FunASRNanoSpeechRecognizer(QObject *parent = nullptr)
        : OfflineSpeechRecognizer(parent)
    {
    }

protected:
    bool configureModel(const nlohmann::json &config,
                        SherpaOnnxOfflineRecognizerConfig *recognizer,
                        QString *errorMessage) override;
    int chunkSeconds() const override;

private:
    std::string m_encoderAdaptorPath;
    std::string m_llmPath;
    std::string m_embeddingPath;
    std::string m_tokenizerPath;
    std::string m_systemPrompt;
    std::string m_userPrompt;
    std::string m_language;
    std::string m_hotwords;
};

} // namespace talkinput
