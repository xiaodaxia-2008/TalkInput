#include "streaming_paraformer_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

bool StreamingParaformerSpeechRecognizer::configureModel(
    const nlohmann::json &config,
    SherpaOnnxOnlineRecognizerConfig *recognizer, QString *errorMessage)
{
    QString encoder;
    QString decoder;
    QString tokens;
    if (!configuredModelPath(config, "encoderFile", &encoder, errorMessage) ||
        !configuredModelPath(config, "decoderFile", &decoder, errorMessage) ||
        !configuredModelPath(config, "tokensFile", &tokens, errorMessage))
    {
        return false;
    }

    m_encoderPath = encoder.toUtf8().toStdString();
    m_decoderPath = decoder.toUtf8().toStdString();
    m_tokensPath = tokens.toUtf8().toStdString();

    recognizer->model_config.paraformer.encoder = m_encoderPath.c_str();
    recognizer->model_config.paraformer.decoder = m_decoderPath.c_str();
    recognizer->model_config.tokens = m_tokensPath.c_str();
    return true;
}

} // namespace talkinput
