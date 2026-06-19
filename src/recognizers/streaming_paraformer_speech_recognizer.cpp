#include "streaming_paraformer_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

std::expected<void, QString> StreamingParaformerSpeechRecognizer::configureModel(
    const nlohmann::json &config,
    SherpaOnnxOnlineRecognizerConfig *recognizer)
{
    auto encoderResult = configuredModelPath(config, "encoderFile");
    if (!encoderResult) return std::unexpected(encoderResult.error());
    auto decoderResult = configuredModelPath(config, "decoderFile");
    if (!decoderResult) return std::unexpected(decoderResult.error());
    auto tokensResult = configuredModelPath(config, "tokensFile");
    if (!tokensResult) return std::unexpected(tokensResult.error());

    m_encoderPath = encoderResult->toUtf8().toStdString();
    m_decoderPath = decoderResult->toUtf8().toStdString();
    m_tokensPath = tokensResult->toUtf8().toStdString();

    recognizer->model_config.paraformer.encoder = m_encoderPath.c_str();
    recognizer->model_config.paraformer.decoder = m_decoderPath.c_str();
    recognizer->model_config.tokens = m_tokensPath.c_str();
    return {};
}

} // namespace talkinput
