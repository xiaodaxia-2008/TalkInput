#include "voice_input_controller.h"
#include "audio_input_capture.h"
#include "logging.h"
#include "text_injector.h"
#include "voice_hotkey.h"
#include "voice_overlay.h"
#include "voice_recognizer_session.h"
#include "voice_text_processor.h"

// ── VoiceInputController ─────────────────────────────────────────

namespace talkinput
{

static VoiceInputController *s_instance = nullptr;

VoiceInputController *VoiceInputController::instance()
{
    return s_instance;
}

VoiceInputController::VoiceInputController(QObject *parent) : QObject(parent)
{
    s_instance = this;
    m_recognizerSession = new VoiceRecognizerSession(this);
    connect(m_recognizerSession, &VoiceRecognizerSession::resultChanged, this,
            &VoiceInputController::onResult);

    m_audioCapture = new AudioInputCapture(this);
    connect(m_audioCapture, &AudioInputCapture::pcm16Ready, this,
            [this](const QByteArray &pcm16, int sampleRate, int channels) {
                m_recognizerSession->feedAudio(pcm16, sampleRate, channels);
            });

    m_hotkey = new VoiceHotkey(this);
    connect(m_hotkey, &VoiceHotkey::activated, this, [this]() {
        if (m_isListening) {
            stopListening();
        }
        else {
            startListening();
        }
    });

    m_overlay = std::make_unique<VoiceOverlay>();
    m_textInjector = new TextInjector(this);
    m_textProcessor = new VoiceTextProcessor(this);
}

VoiceInputController::~VoiceInputController()
{
    stopListening();
    s_instance = nullptr;
}

bool VoiceInputController::startListening()
{
    SPDLOG_INFO("VoiceInputController: start listening");

    if (m_isListening) {
        SPDLOG_WARN("Already listening");
        return false;
    }

    if (!m_recognizerSession->isModelLoaded()) {
        SPDLOG_WARN("ASR model not loaded");
        spdlog::get("statusbar")
            ->info("{}",
                   tr("Model not loaded yet. Please wait or select a model."));
        return false;
    }

    m_recognizerSession->resetStream();

    if (!m_recognizerSession->acceptsExternalAudio()) {
        enterListeningState(
            "Voice input started with recognizer-owned audio source");
        return true;
    }

    auto captureStarted = m_audioCapture->start();
    if (!captureStarted) {
        spdlog::get("statusbar")->info("{}", captureStarted.error());
        return false;
    }

    enterListeningState("Voice input started");
    return true;
}

void VoiceInputController::stopListening()
{
    SPDLOG_INFO("VoiceInputController: stop listening");

    if (m_audioCapture) {
        m_audioCapture->stop();
    }

    if (m_recognizerSession->finishRunningStream()) {
        m_pendingResult = true;
    }

    leaveListeningState();
}

void VoiceInputController::onResult(const QString &text, bool isFinal)
{
    if (isFinal) {
        m_lastResult = text;
        const bool shouldSend = m_pendingResult || !m_isListening;
        m_pendingResult = false;
        if (shouldSend && !text.trimmed().isEmpty()) {
            postProcessFinalText(text.trimmed());
        }
    }
    else if (m_isListening && text != m_lastResult) {
        m_lastResult = text;
        if (m_overlay) {
            m_overlay->setPreviewText(text);
        }
    }
}

void VoiceInputController::postProcessFinalText(const QString &text)
{
    m_textProcessor->processFinalText(
        text, this, [this](const QString &processedText) {
            injectFinalText(processedText.trimmed());
        });
}

void VoiceInputController::injectFinalText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    if (!m_textInjector->inject(text)) {
        return;
    }

    emit finalTextCommitted(text);
    SPDLOG_INFO("VoiceInputController injected and saved: {}", text);
}

void VoiceInputController::enterListeningState(const char *logMessage)
{
    m_isListening = true;
    m_lastResult.clear();
    showOverlay();
    emit listeningChanged(true);
    SPDLOG_INFO("{}", logMessage);
}

void VoiceInputController::leaveListeningState()
{
    m_isListening = false;
    hideOverlay();
    emit listeningChanged(false);
}

void VoiceInputController::showOverlay()
{
    if (m_overlay) {
        m_overlay->startAnimation();
    }
}

void VoiceInputController::hideOverlay()
{
    if (m_overlay) {
        m_overlay->stopAnimation();
    }
}

// ── SpeechRecognizer lifecycle ──────────────────────────────────

void VoiceInputController::loadModel(const nlohmann::json &preset)
{
    const auto result = m_recognizerSession->loadModel(preset);
    if (!result) {
        SPDLOG_ERROR("VoiceInputController: model load failed: {}",
                     result.error());
        emit modelLoadResult(false, result.error());
        return;
    }

    emit modelLoadResult(true, {});
}

void VoiceInputController::unloadModel()
{
    m_recognizerSession->unloadModel();
}

void VoiceInputController::startSession()
{
    m_recognizerSession->resetStream();
}

void VoiceInputController::feedAudio(const QByteArray &pcm16, int sampleRate,
                                     int channels)
{
    m_recognizerSession->feedAudio(pcm16, sampleRate, channels);
}

void VoiceInputController::finishSession()
{
    m_recognizerSession->finishRunningStream();
}

bool VoiceInputController::acceptsExternalAudio() const
{
    return !m_recognizerSession || m_recognizerSession->acceptsExternalAudio();
}

bool VoiceInputController::isModelLoaded() const
{
    return m_recognizerSession && m_recognizerSession->isModelLoaded();
}

SpeechRecognizer *VoiceInputController::recognizer() const
{
    return m_recognizerSession ? m_recognizerSession->recognizer() : nullptr;
}

} // namespace talkinput
