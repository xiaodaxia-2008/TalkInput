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
    m_recognizerSession = std::make_unique<VoiceRecognizerSession>();
    connect(m_recognizerSession.get(), &VoiceRecognizerSession::resultChanged,
            this, &VoiceInputController::onResult);

    m_audioCapture = std::make_unique<AudioInputCapture>();
    connect(m_audioCapture.get(), &AudioInputCapture::pcm16Ready, this,
            [this](const QByteArray &pcm16, int sampleRate, int channels) {
                m_recognizerSession->feedRecognitionAudio(pcm16, sampleRate,
                                                          channels);
            });

    m_hotkey = std::make_unique<VoiceHotkey>();
    connect(m_hotkey.get(), &VoiceHotkey::activated, this, [this]() {
        if (m_isListening) {
            stopListening();
        }
        else {
            startListening(FinalTextAction::PasteAndRecordHistory);
        }
    });

    m_overlay = std::make_unique<VoiceOverlay>();
    m_textInjector = std::make_unique<TextInjector>();
    m_textProcessor = std::make_unique<VoiceTextProcessor>();
}

VoiceInputController::~VoiceInputController()
{
    stopListening();
    s_instance = nullptr;
}

bool VoiceInputController::startListening()
{
    return startListening(FinalTextAction::RecordHistoryOnly);
}

bool VoiceInputController::startListening(FinalTextAction finalTextAction)
{
    SPDLOG_INFO("VoiceInputController: start listening");

    if (!beginRecognitionFlow(finalTextAction)) {
        return false;
    }

    if (!m_recognizerSession->isSpeechRecognitionModelLoaded()) {
        SPDLOG_WARN("Speech recognition model not loaded");
        STATUSBAR_INFO("{}",
                       tr("Speech recognition model not loaded yet. Please "
                          "wait or select a model."));
        resetRecognitionFlow();
        return false;
    }

    m_recognizerSession->resetRecognitionStream();

    if (!m_recognizerSession->acceptsExternalAudio()) {
        enterListeningState(
            "Voice input started with recognizer-owned audio source");
        return true;
    }

    auto captureStarted = m_audioCapture->start();
    if (!captureStarted) {
        STATUSBAR_INFO("{}", captureStarted.error());
        resetRecognitionFlow();
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

    if (!m_recognizerSession->finishRunningRecognitionStream()) {
        resetRecognitionFlow();
        return;
    }

    if (m_isListening) {
        leaveListeningState();
    }
    if (!m_processingFinalText && m_recognizerSession->acceptsExternalAudio()) {
        resetRecognitionFlow();
    }
}

void VoiceInputController::onResult(const QString &text, bool isFinal)
{
    if (isFinal) {
        m_lastResult = text;
        if (!m_busy || m_processingFinalText) {
            return;
        }

        const QString finalText = text.trimmed();
        if (finalText.isEmpty()) {
            resetRecognitionFlow();
            return;
        }

        m_processingFinalText = true;
        if (m_isListening) {
            leaveListeningState();
        }
        if (m_recognizerSession &&
            !m_recognizerSession->acceptsExternalAudio() &&
            m_recognizerSession->isRecognitionStreamRunning())
        {
            m_recognizerSession->finishRunningRecognitionStream();
        }
        postProcessFinalText(finalText, m_finalTextAction);
    }
    else if (m_isListening && text != m_lastResult) {
        m_lastResult = text;
        if (m_overlay) {
            m_overlay->setPreviewText(text);
        }
    }
}

void VoiceInputController::postProcessFinalText(const QString &text,
                                                FinalTextAction finalTextAction)
{
    SPDLOG_INFO("VoiceInputController processing final text");
    m_textProcessor->processFinalText(
        text, this, [this, finalTextAction](const QString &processedText) {
            commitFinalText(processedText.trimmed(), finalTextAction);
        });
}

void VoiceInputController::commitFinalText(const QString &text,
                                           FinalTextAction finalTextAction)
{
    if (text.isEmpty()) {
        resetRecognitionFlow();
        return;
    }

    if (finalTextAction == FinalTextAction::PasteAndRecordHistory) {
        if (m_textInjector->inject(text)) {
            SPDLOG_INFO("VoiceInputController pasted final text");
        }
        else {
            SPDLOG_WARN("VoiceInputController failed to paste final text");
        }
    }
    else {
        SPDLOG_INFO("VoiceInputController recorded final text without paste");
    }

    emit finalTextCommitted(text);
    SPDLOG_INFO("VoiceInputController saved final text to history: {}", text);
    resetRecognitionFlow();
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

bool VoiceInputController::beginRecognitionFlow(FinalTextAction finalTextAction)
{
    if (m_busy) {
        SPDLOG_WARN("Recognition flow is busy");
        STATUSBAR_INFO("{}", tr("Recognition is still processing."));
        return false;
    }

    m_busy = true;
    m_processingFinalText = false;
    m_finalTextAction = finalTextAction;
    m_lastResult.clear();
    return true;
}

void VoiceInputController::resetRecognitionFlow()
{
    if (m_isListening) {
        leaveListeningState();
    }
    m_busy = false;
    m_processingFinalText = false;
    m_finalTextAction = FinalTextAction::RecordHistoryOnly;
    m_lastResult.clear();
}

// ── SpeechRecognizer lifecycle ──────────────────────────────────

void VoiceInputController::loadSpeechRecognitionModel(
    const nlohmann::json &preset)
{
    const auto result = m_recognizerSession->loadSpeechRecognitionModel(preset);
    if (!result) {
        SPDLOG_ERROR("VoiceInputController: speech recognition model load "
                     "failed: {}",
                     result.error());
        emit speechRecognitionModelLoadResult(false, result.error());
        return;
    }

    emit speechRecognitionModelLoadResult(true, {});
}

void VoiceInputController::unloadSpeechRecognitionModel()
{
    m_recognizerSession->unloadSpeechRecognitionModel();
}

bool VoiceInputController::startSpeechRecognitionSession()
{
    if (!beginRecognitionFlow(FinalTextAction::RecordHistoryOnly)) {
        return false;
    }

    if (!m_recognizerSession->isSpeechRecognitionModelLoaded()) {
        SPDLOG_WARN("Speech recognition model not loaded");
        resetRecognitionFlow();
        return false;
    }

    m_recognizerSession->resetRecognitionStream();
    return true;
}

void VoiceInputController::feedSpeechRecognitionAudio(const QByteArray &pcm16,
                                                      int sampleRate,
                                                      int channels)
{
    m_recognizerSession->feedRecognitionAudio(pcm16, sampleRate, channels);
}

void VoiceInputController::finishSpeechRecognitionSession()
{
    if (!m_recognizerSession->finishRunningRecognitionStream()) {
        resetRecognitionFlow();
        return;
    }

    if (!m_processingFinalText && m_recognizerSession->acceptsExternalAudio()) {
        resetRecognitionFlow();
    }
}

bool VoiceInputController::acceptsExternalAudio() const
{
    return !m_recognizerSession || m_recognizerSession->acceptsExternalAudio();
}

bool VoiceInputController::isSpeechRecognitionModelLoaded() const
{
    return m_recognizerSession &&
           m_recognizerSession->isSpeechRecognitionModelLoaded();
}

SpeechRecognizer *VoiceInputController::speechRecognizer() const
{
    return m_recognizerSession ? m_recognizerSession->speechRecognizer()
                               : nullptr;
}

} // namespace talkinput
