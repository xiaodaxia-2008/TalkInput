#include "sense_voice_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

namespace talkinput
{

std::expected<void, QString> SenseVoiceSpeechRecognizer::configureModel(
    SherpaOnnxOfflineRecognizerConfig *recognizer)
{
    const auto &files = m_preset.resolvedFiles;
    auto it = files.find("senseVoiceModelFile");
    if (it == files.end()) return std::unexpected(QStringLiteral("Missing senseVoiceModelFile"));
    auto it2 = files.find("tokensFile");
    if (it2 == files.end()) return std::unexpected(QStringLiteral("Missing tokensFile"));

    m_modelPath = it->second;
    m_tokensPath = it2->second;
    m_language = m_preset.params.language;

    recognizer->model_config.sense_voice.model = m_modelPath.c_str();
    recognizer->model_config.sense_voice.language = m_language.c_str();
    recognizer->model_config.sense_voice.use_itn =
        m_preset.params.senseVoiceUseItn ? 1 : 0;
    recognizer->model_config.tokens = m_tokensPath.c_str();
    return {};
}

} // namespace talkinput
