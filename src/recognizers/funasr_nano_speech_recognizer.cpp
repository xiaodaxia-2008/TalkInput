#include "funasr_nano_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

std::expected<void, QString> FunASRNanoSpeechRecognizer::configureModel(
    const nlohmann::json &config,
    SherpaOnnxOfflineRecognizerConfig *recognizer)
{
    auto adaptorResult = configuredModelPath(config, "funasrEncoderAdaptorFile");
    if (!adaptorResult) return std::unexpected(adaptorResult.error());
    auto llmResult = configuredModelPath(config, "funasrLlmFile");
    if (!llmResult) return std::unexpected(llmResult.error());
    auto embeddingResult = configuredModelPath(config, "funasrEmbeddingFile");
    if (!embeddingResult) return std::unexpected(embeddingResult.error());
    auto tokenizerResult = configuredModelPath(config, "funasrTokenizerFile");
    if (!tokenizerResult) return std::unexpected(tokenizerResult.error());

    const nlohmann::json params =
        config.value("params", nlohmann::json::object());

    m_encoderAdaptorPath = adaptorResult->toUtf8().toStdString();
    m_llmPath = llmResult->toUtf8().toStdString();
    m_embeddingPath = embeddingResult->toUtf8().toStdString();
    m_tokenizerPath = tokenizerResult->toUtf8().toStdString();
    m_systemPrompt =
        jsonString(params, "funasrSystemPrompt", "You are a helpful assistant.")
            .toUtf8()
            .toStdString();
    m_userPrompt = jsonString(params, "funasrUserPrompt", "语音转写：")
                       .toUtf8()
                       .toStdString();
    m_language = jsonString(params, "language", "zh").toUtf8().toStdString();
    m_hotwords = jsonString(config, "hotwordsText").toUtf8().toStdString();

    recognizer->model_config.funasr_nano.encoder_adaptor =
        m_encoderAdaptorPath.c_str();
    recognizer->model_config.funasr_nano.llm = m_llmPath.c_str();
    recognizer->model_config.funasr_nano.embedding = m_embeddingPath.c_str();
    recognizer->model_config.funasr_nano.tokenizer = m_tokenizerPath.c_str();
    recognizer->model_config.funasr_nano.system_prompt = m_systemPrompt.c_str();
    recognizer->model_config.funasr_nano.user_prompt = m_userPrompt.c_str();
    recognizer->model_config.funasr_nano.max_new_tokens =
        jsonInt(params, "funasrMaxNewTokens", 128);
    recognizer->model_config.funasr_nano.temperature =
        jsonFloat(params, "funasrTemperature", 1e-6F);
    recognizer->model_config.funasr_nano.top_p =
        jsonFloat(params, "funasrTopP", 0.8F);
    recognizer->model_config.funasr_nano.seed =
        jsonInt(params, "funasrSeed", 42);
    recognizer->model_config.funasr_nano.language = m_language.c_str();
    recognizer->model_config.funasr_nano.itn =
        jsonBool(params, "funasrItn", true) ? 1 : 0;
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
