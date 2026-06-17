#include "funasr_nano_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

bool FunASRNanoRecognizer::configureModel(
    const Config &config, SherpaOnnxOfflineRecognizerConfig *recognizer,
    QString *errorMessage)
{
    const QString adaptor =
        modelPath(config.modelDir, config.funasrEncoderAdaptorFile);
    const QString llm = modelPath(config.modelDir, config.funasrLlmFile);
    const QString embedding =
        modelPath(config.modelDir, config.funasrEmbeddingFile);
    const QString tokenizer =
        modelPath(config.modelDir, config.funasrTokenizerFile);

    if (!fileExists(adaptor, errorMessage) || !fileExists(llm, errorMessage) ||
        !fileExists(embedding, errorMessage) ||
        !pathExists(tokenizer, errorMessage))
    {
        return false;
    }

    m_encoderAdaptorPath = adaptor.toUtf8().toStdString();
    m_llmPath = llm.toUtf8().toStdString();
    m_embeddingPath = embedding.toUtf8().toStdString();
    m_tokenizerPath = tokenizer.toUtf8().toStdString();
    m_systemPrompt = config.funasrSystemPrompt.toUtf8().toStdString();
    m_userPrompt = config.funasrUserPrompt.toUtf8().toStdString();
    m_language = config.funasrLanguage.toUtf8().toStdString();
    m_hotwords = config.hotwordsText.toUtf8().toStdString();

    recognizer->model_config.funasr_nano.encoder_adaptor =
        m_encoderAdaptorPath.c_str();
    recognizer->model_config.funasr_nano.llm = m_llmPath.c_str();
    recognizer->model_config.funasr_nano.embedding = m_embeddingPath.c_str();
    recognizer->model_config.funasr_nano.tokenizer = m_tokenizerPath.c_str();
    recognizer->model_config.funasr_nano.system_prompt = m_systemPrompt.c_str();
    recognizer->model_config.funasr_nano.user_prompt = m_userPrompt.c_str();
    recognizer->model_config.funasr_nano.max_new_tokens =
        config.funasrMaxNewTokens;
    recognizer->model_config.funasr_nano.temperature = config.funasrTemperature;
    recognizer->model_config.funasr_nano.top_p = config.funasrTopP;
    recognizer->model_config.funasr_nano.seed = config.funasrSeed;
    recognizer->model_config.funasr_nano.language = m_language.c_str();
    recognizer->model_config.funasr_nano.itn = config.funasrItn ? 1 : 0;
    if (!m_hotwords.empty()) {
        recognizer->model_config.funasr_nano.hotwords = m_hotwords.c_str();
    }
    return true;
}

int FunASRNanoRecognizer::chunkSeconds() const
{
    return 10;
}

} // namespace talkinput
