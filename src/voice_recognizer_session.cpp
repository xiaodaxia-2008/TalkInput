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
    unloadModel();
}

bool VoiceRecognizerSession::isModelLoaded() const
{
    return m_recognizer != nullptr;
}

bool VoiceRecognizerSession::acceptsExternalAudio() const
{
    return !m_recognizer || m_recognizer->acceptsExternalAudio();
}

SpeechRecognizer *VoiceRecognizerSession::recognizer() const
{
    return m_recognizer.get();
}

std::expected<void, QString>
VoiceRecognizerSession::loadModel(const nlohmann::json &preset)
{
    unloadModel();

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

void VoiceRecognizerSession::unloadModel()
{
    if (m_recognizer && m_recognizer->isRunning()) {
        m_recognizer->stop();
    }
    m_recognizer.reset();
}

void VoiceRecognizerSession::resetStream()
{
    if (m_recognizer) {
        m_recognizer->resetStream();
    }
}

void VoiceRecognizerSession::feedAudio(const QByteArray &pcm16, int sampleRate,
                                       int channels)
{
    if (m_recognizer) {
        m_recognizer->acceptPcm16(pcm16, sampleRate, channels);
    }
}

bool VoiceRecognizerSession::finishRunningStream()
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
