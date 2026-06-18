#include "sense_voice_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

bool SenseVoiceSpeechRecognizer::configureModel(
    const nlohmann::json &config,
    SherpaOnnxOfflineRecognizerConfig *recognizer, QString *errorMessage)
{
    QString model;
    QString tokens;
    if (!configuredModelPath(config, "senseVoiceModelFile", &model,
                             errorMessage) ||
        !configuredModelPath(config, "tokensFile", &tokens, errorMessage))
    {
        return false;
    }

    const nlohmann::json params =
        config.value("params", nlohmann::json::object());

    m_modelPath = model.toUtf8().toStdString();
    m_tokensPath = tokens.toUtf8().toStdString();
    m_language = jsonString(params, "language", "zh").toUtf8().toStdString();

    recognizer->model_config.sense_voice.model = m_modelPath.c_str();
    recognizer->model_config.sense_voice.language = m_language.c_str();
    recognizer->model_config.sense_voice.use_itn =
        jsonBool(params, "senseVoiceUseItn", true) ? 1 : 0;
    recognizer->model_config.tokens = m_tokensPath.c_str();
    return true;
}

} // namespace talkinput
