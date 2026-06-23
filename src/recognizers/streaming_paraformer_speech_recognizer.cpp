#include "streaming_paraformer_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

std::expected<void, QString>
StreamingParaformerSpeechRecognizer::configureModel(
    SherpaOnnxOnlineRecognizerConfig *recognizer)
{
    const auto &files = m_preset.resolvedFiles;
    auto it = files.find("encoderFile");
    if (it == files.end()) return std::unexpected(QStringLiteral("Missing encoderFile"));
    auto it2 = files.find("decoderFile");
    if (it2 == files.end()) return std::unexpected(QStringLiteral("Missing decoderFile"));
    auto it3 = files.find("tokensFile");
    if (it3 == files.end()) return std::unexpected(QStringLiteral("Missing tokensFile"));

    m_encoderPath = it->second;
    m_decoderPath = it2->second;
    m_tokensPath = it3->second;

    recognizer->model_config.paraformer.encoder = m_encoderPath.c_str();
    recognizer->model_config.paraformer.decoder = m_decoderPath.c_str();
    recognizer->model_config.tokens = m_tokensPath.c_str();
    return {};
}

} // namespace talkinput
