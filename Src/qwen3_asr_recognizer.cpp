#include "qwen3_asr_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

bool Qwen3ASRRecognizer::configureModel(
    const Config &config, SherpaOnnxOfflineRecognizerConfig *recognizer,
    QString *errorMessage)
{
    const QString frontend =
        modelPath(config.modelDir, config.qwen3ConvFrontendFile);
    const QString encoder = modelPath(config.modelDir, config.qwen3EncoderFile);
    const QString decoder = modelPath(config.modelDir, config.qwen3DecoderFile);

    if (!fileExists(frontend, errorMessage) ||
        !fileExists(encoder, errorMessage) ||
        !fileExists(decoder, errorMessage) ||
        !pathExists(config.qwen3TokenizerDir, errorMessage))
    {
        return false;
    }

    m_convFrontendPath = frontend.toUtf8().toStdString();
    m_encoderPath = encoder.toUtf8().toStdString();
    m_decoderPath = decoder.toUtf8().toStdString();
    m_tokenizerDir = config.qwen3TokenizerDir.toUtf8().toStdString();
    m_hotwords = config.hotwordsText.toUtf8().toStdString();

    recognizer->model_config.qwen3_asr.conv_frontend =
        m_convFrontendPath.c_str();
    recognizer->model_config.qwen3_asr.encoder = m_encoderPath.c_str();
    recognizer->model_config.qwen3_asr.decoder = m_decoderPath.c_str();
    recognizer->model_config.qwen3_asr.tokenizer = m_tokenizerDir.c_str();
    recognizer->model_config.qwen3_asr.max_total_len = config.qwen3MaxTotalLen;
    recognizer->model_config.qwen3_asr.max_new_tokens =
        config.qwen3MaxNewTokens;
    recognizer->model_config.qwen3_asr.temperature = config.qwen3Temperature;
    recognizer->model_config.qwen3_asr.top_p = config.qwen3TopP;
    recognizer->model_config.qwen3_asr.seed = config.qwen3Seed;
    if (!m_hotwords.empty()) {
        recognizer->model_config.qwen3_asr.hotwords = m_hotwords.c_str();
    }
    return true;
}

} // namespace talkinput
