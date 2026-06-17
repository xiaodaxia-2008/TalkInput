#include "streaming_paraformer_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

bool StreamingParaformerRecognizer::configureModel(
    const Config &config, SherpaOnnxOnlineRecognizerConfig *recognizer,
    QString *errorMessage)
{
    const QString encoder = modelPath(config.modelDir, config.encoderFile);
    const QString decoder = modelPath(config.modelDir, config.decoderFile);
    const QString tokens = modelPath(config.modelDir, config.tokensFile);

    if (!fileExists(encoder, errorMessage) ||
        !fileExists(decoder, errorMessage) || !fileExists(tokens, errorMessage))
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
