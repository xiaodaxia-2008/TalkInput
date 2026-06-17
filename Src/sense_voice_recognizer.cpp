#include "sense_voice_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

bool SenseVoiceRecognizer::configureModel(
    const Config &config, SherpaOnnxOfflineRecognizerConfig *recognizer,
    QString *errorMessage)
{
    const QString model =
        modelPath(config.modelDir, config.senseVoiceModelFile);
    const QString tokens = modelPath(config.modelDir, config.tokensFile);

    if (!fileExists(model, errorMessage) || !fileExists(tokens, errorMessage)) {
        return false;
    }

    m_modelPath = model.toUtf8().toStdString();
    m_tokensPath = tokens.toUtf8().toStdString();
    m_language = config.senseVoiceLanguage.toUtf8().toStdString();

    recognizer->model_config.sense_voice.model = m_modelPath.c_str();
    recognizer->model_config.sense_voice.language = m_language.c_str();
    recognizer->model_config.sense_voice.use_itn =
        config.senseVoiceUseItn ? 1 : 0;
    recognizer->model_config.tokens = m_tokensPath.c_str();
    return true;
}

} // namespace talkinput
