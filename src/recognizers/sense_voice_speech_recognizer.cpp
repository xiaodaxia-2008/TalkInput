#include "sense_voice_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

std::expected<void, QString> SenseVoiceSpeechRecognizer::configureModel(
    const nlohmann::json &config,
    SherpaOnnxOfflineRecognizerConfig *recognizer)
{
    auto modelResult = configuredModelPath(config, "senseVoiceModelFile");
    if (!modelResult) return std::unexpected(modelResult.error());
    auto tokensResult = configuredModelPath(config, "tokensFile");
    if (!tokensResult) return std::unexpected(tokensResult.error());

    const nlohmann::json params =
        config.value("params", nlohmann::json::object());

    m_modelPath = modelResult->toUtf8().toStdString();
    m_tokensPath = tokensResult->toUtf8().toStdString();
    m_language = jsonString(params, "language", "zh").toUtf8().toStdString();

    recognizer->model_config.sense_voice.model = m_modelPath.c_str();
    recognizer->model_config.sense_voice.language = m_language.c_str();
    recognizer->model_config.sense_voice.use_itn =
        jsonBool(params, "senseVoiceUseItn", true) ? 1 : 0;
    recognizer->model_config.tokens = m_tokensPath.c_str();
    return {};
}

} // namespace talkinput
