#include "sense_voice_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

bool SenseVoiceSpeechRecognizer::configureModel(
    const Config &config, SherpaOnnxOfflineRecognizerConfig *recognizer,
    QString *errorMessage)
{
    QString model;
    QString tokens;
    if (!configuredModelPath(config, "senseVoiceModelFile", &model,
                             errorMessage) ||
        !configuredModelPath(config, "tokensFile", &tokens, errorMessage))
    {
        return false;
    }

    m_modelPath = model.toUtf8().toStdString();
    m_tokensPath = tokens.toUtf8().toStdString();
    m_language = config.language.toUtf8().toStdString();

    recognizer->model_config.sense_voice.model = m_modelPath.c_str();
    recognizer->model_config.sense_voice.language = m_language.c_str();
    recognizer->model_config.sense_voice.use_itn =
        config.senseVoiceUseItn ? 1 : 0;
    recognizer->model_config.tokens = m_tokensPath.c_str();
    return true;
}

} // namespace talkinput
