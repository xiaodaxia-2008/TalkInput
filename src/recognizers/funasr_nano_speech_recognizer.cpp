#include "funasr_nano_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

std::expected<void, QString> FunASRNanoSpeechRecognizer::configureModel(
    SherpaOnnxOfflineRecognizerConfig *recognizer)
{
    const auto &files = m_preset.resolvedFiles;
    auto it = files.find("funasrEncoderAdaptorFile");
    if (it == files.end()) return std::unexpected(QStringLiteral("Missing funasrEncoderAdaptorFile"));
    auto it2 = files.find("funasrLlmFile");
    if (it2 == files.end()) return std::unexpected(QStringLiteral("Missing funasrLlmFile"));
    auto it3 = files.find("funasrEmbeddingFile");
    if (it3 == files.end()) return std::unexpected(QStringLiteral("Missing funasrEmbeddingFile"));
    auto it4 = files.find("funasrTokenizerFile");
    if (it4 == files.end()) return std::unexpected(QStringLiteral("Missing funasrTokenizerFile"));

    const auto &params = m_preset.params;

    recognizer->model_config.funasr_nano.encoder_adaptor =
        it->second.c_str();
    recognizer->model_config.funasr_nano.llm = it2->second.c_str();
    recognizer->model_config.funasr_nano.embedding = it3->second.c_str();
    recognizer->model_config.funasr_nano.tokenizer = it4->second.c_str();
    recognizer->model_config.funasr_nano.system_prompt =
        params.funasrSystemPrompt.c_str();
    recognizer->model_config.funasr_nano.user_prompt =
        params.funasrUserPrompt.c_str();
    recognizer->model_config.funasr_nano.max_new_tokens = params.funasrMaxNewTokens;
    recognizer->model_config.funasr_nano.temperature =
        static_cast<float>(params.funasrTemperature);
    recognizer->model_config.funasr_nano.top_p =
        static_cast<float>(params.funasrTopP);
    recognizer->model_config.funasr_nano.seed = params.funasrSeed;
    recognizer->model_config.funasr_nano.language = params.language.c_str();
    recognizer->model_config.funasr_nano.itn = params.funasrItn ? 1 : 0;
    if (!m_preset.hotwordsText.empty()) {
        recognizer->model_config.funasr_nano.hotwords =
            m_preset.hotwordsText.c_str();
    }
    return {};
}

} // namespace talkinput
