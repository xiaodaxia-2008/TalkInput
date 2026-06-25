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

    recognizer->model_config.paraformer.encoder = it->second.c_str();
    recognizer->model_config.paraformer.decoder = it2->second.c_str();
    recognizer->model_config.tokens = it3->second.c_str();
    return {};
}

} // namespace talkinput
