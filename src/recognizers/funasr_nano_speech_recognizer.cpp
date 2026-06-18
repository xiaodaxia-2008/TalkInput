#include "funasr_nano_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

bool FunASRNanoSpeechRecognizer::configureModel(
    const nlohmann::json &config,
    SherpaOnnxOfflineRecognizerConfig *recognizer, QString *errorMessage)
{
    QString adaptor;
    QString llm;
    QString embedding;
    QString tokenizer;
    if (!configuredModelPath(config, "funasrEncoderAdaptorFile", &adaptor,
                             errorMessage) ||
        !configuredModelPath(config, "funasrLlmFile", &llm, errorMessage) ||
        !configuredModelPath(config, "funasrEmbeddingFile", &embedding,
                             errorMessage) ||
        !configuredModelPath(config, "funasrTokenizerFile", &tokenizer,
                             errorMessage))
    {
        return false;
    }

    const nlohmann::json params =
        config.value("params", nlohmann::json::object());

    m_encoderAdaptorPath = adaptor.toUtf8().toStdString();
    m_llmPath = llm.toUtf8().toStdString();
    m_embeddingPath = embedding.toUtf8().toStdString();
    m_tokenizerPath = tokenizer.toUtf8().toStdString();
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
    return true;
}

int FunASRNanoSpeechRecognizer::chunkSeconds() const
{
    return 10;
}

} // namespace talkinput
