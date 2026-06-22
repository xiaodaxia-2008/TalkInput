#include "funasr_nano_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

std::expected<void, QString> FunASRNanoSpeechRecognizer::configureModel(
    const AsrPreset &preset,
    SherpaOnnxOfflineRecognizerConfig *recognizer)
{
    const auto &files = preset.resolvedFiles;
    auto it = files.find("funasrEncoderAdaptorFile");
    if (it == files.end()) return std::unexpected(QStringLiteral("Missing funasrEncoderAdaptorFile"));
    auto it2 = files.find("funasrLlmFile");
    if (it2 == files.end()) return std::unexpected(QStringLiteral("Missing funasrLlmFile"));
    auto it3 = files.find("funasrEmbeddingFile");
    if (it3 == files.end()) return std::unexpected(QStringLiteral("Missing funasrEmbeddingFile"));
    auto it4 = files.find("funasrTokenizerFile");
    if (it4 == files.end()) return std::unexpected(QStringLiteral("Missing funasrTokenizerFile"));

    const auto &params = preset.params;

    m_encoderAdaptorPath = it->second;
    m_llmPath = it2->second;
    m_embeddingPath = it3->second;
    m_tokenizerPath = it4->second;
    m_systemPrompt = params.funasrSystemPrompt;
    m_userPrompt = params.funasrUserPrompt;
    m_language = params.language;
    m_hotwords = preset.hotwordsText;

    recognizer->model_config.funasr_nano.encoder_adaptor =
        m_encoderAdaptorPath.c_str();
    recognizer->model_config.funasr_nano.llm = m_llmPath.c_str();
    recognizer->model_config.funasr_nano.embedding = m_embeddingPath.c_str();
    recognizer->model_config.funasr_nano.tokenizer = m_tokenizerPath.c_str();
    recognizer->model_config.funasr_nano.system_prompt = m_systemPrompt.c_str();
    recognizer->model_config.funasr_nano.user_prompt = m_userPrompt.c_str();
    recognizer->model_config.funasr_nano.max_new_tokens = params.funasrMaxNewTokens;
    recognizer->model_config.funasr_nano.temperature =
        static_cast<float>(params.funasrTemperature);
    recognizer->model_config.funasr_nano.top_p =
        static_cast<float>(params.funasrTopP);
    recognizer->model_config.funasr_nano.seed = params.funasrSeed;
    recognizer->model_config.funasr_nano.language = m_language.c_str();
    recognizer->model_config.funasr_nano.itn = params.funasrItn ? 1 : 0;
    if (!m_hotwords.empty()) {
        recognizer->model_config.funasr_nano.hotwords = m_hotwords.c_str();
    }
    return {};
}

int FunASRNanoSpeechRecognizer::chunkSeconds() const
{
    return 10;
}

} // namespace talkinput
