#include "voice_recognizer_session.h"
#include "asr_config.h"
#include "speech_recognizer.h"

#include <utility>

namespace talkinput
{

VoiceRecognizerSession::VoiceRecognizerSession(QObject *parent)
    : QObject(parent)
{
}

VoiceRecognizerSession::~VoiceRecognizerSession()
{
    unloadSpeechRecognitionModel();
}

bool VoiceRecognizerSession::isSpeechRecognitionModelLoaded() const
{
    return m_recognizer != nullptr;
}

bool VoiceRecognizerSession::acceptsExternalAudio() const
{
    return !m_recognizer || m_recognizer->acceptsExternalAudio();
}

SpeechRecognizer *VoiceRecognizerSession::speechRecognizer() const
{
    return m_recognizer.get();
}

std::expected<void, QString>
VoiceRecognizerSession::loadSpeechRecognitionModel(const nlohmann::json &preset)
{
    unloadSpeechRecognitionModel();

    const QString modelDir = asrModelDir(preset);
    auto recognizer = SpeechRecognizer::createFromConfig(
        preset, modelDir, currentHotwordsConfig(), this);
    if (!recognizer) {
        return std::unexpected(recognizer.error());
    }

    connect(recognizer->get(), &SpeechRecognizer::resultChanged, this,
            &VoiceRecognizerSession::resultChanged);
    m_recognizer = std::move(*recognizer);

    setCurrentAsrProviderId(jsonString(preset, "id"));
    return {};
}

void VoiceRecognizerSession::unloadSpeechRecognitionModel()
{
    if (m_recognizer && m_recognizer->isRunning()) {
        m_recognizer->stop();
    }
    m_recognizer.reset();
}

void VoiceRecognizerSession::resetRecognitionStream()
{
    if (m_recognizer) {
        m_recognizer->resetStream();
    }
}

void VoiceRecognizerSession::feedRecognitionAudio(const QByteArray &pcm16,
                                                  int sampleRate, int channels)
{
    if (m_recognizer) {
        m_recognizer->acceptPcm16(pcm16, sampleRate, channels);
    }
}

bool VoiceRecognizerSession::finishRunningRecognitionStream()
{
    if (!m_recognizer || !m_recognizer->isRunning()) {
        return false;
    }

    m_recognizer->finish();
    if (m_recognizer->acceptsExternalAudio()) {
        m_recognizer->resetStream();
    }
    return true;
}

} // namespace talkinput
