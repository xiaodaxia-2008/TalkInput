#include "voice_input_controller.h"
#include "asr_config.h"
#include "audio_input_capture.h"
#include "logging.h"
#include "paste_text.h"
#include "voice_overlay.h"
#include "voice_text_processor.h"

#include <QHotkey>

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
    m_audioCapture = new AudioInputCapture(this);
    connect(m_audioCapture, &AudioInputCapture::pcm16Ready, this,
            [this](const QByteArray &pcm16, int sampleRate, int channels) {
                if (m_recognizer) {
                    m_recognizer->acceptPcm16(pcm16, sampleRate, channels);
                }
            });

    m_overlay = std::make_unique<VoiceOverlay>();
    m_textProcessor = new VoiceTextProcessor(this);
    registerHotKey();
}

VoiceInputController::~VoiceInputController()
{
    stopListening();
    unregisterHotKey();
    s_instance = nullptr;
}

bool VoiceInputController::startListening()
{
    SPDLOG_INFO("VoiceInputController: start listening");

    if (m_isListening) {
        SPDLOG_WARN("Already listening");
        return false;
    }

    if (!m_recognizer) {
        SPDLOG_WARN("ASR model not loaded");
        spdlog::get("statusbar")
            ->info("{}",
                   tr("Model not loaded yet. Please wait or select a model."));
        return false;
    }

    m_recognizer->resetStream();

    if (!m_recognizer->acceptsExternalAudio()) {
        m_isListening = true;
        m_lastResult.clear();
        showOverlay();
        emit listeningChanged(true);
        SPDLOG_INFO("Voice input started with recognizer-owned audio source");
        return true;
    }

    auto captureStarted = m_audioCapture->start();
    if (!captureStarted) {
        spdlog::get("statusbar")->info("{}", captureStarted.error());
        return false;
    }

    m_isListening = true;
    m_lastResult.clear();
    showOverlay();
    emit listeningChanged(true);
    SPDLOG_INFO("Voice input started");
    return true;
}

void VoiceInputController::stopListening()
{
    SPDLOG_INFO("VoiceInputController: stop listening");

    if (m_audioCapture) {
        m_audioCapture->stop();
    }

    if (m_recognizer && m_recognizer->isRunning()) {
        m_pendingResult = true;
        m_recognizer->finish();
        if (m_recognizer->acceptsExternalAudio()) {
            m_recognizer->resetStream();
        }
    }

    m_isListening = false;
    hideOverlay();
    emit listeningChanged(false);
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

    sendText(text);
    emit finalTextCommitted(text);
    SPDLOG_INFO("VoiceInputController injected and saved: {}", text);
}

// ── VoiceInputController::sendText ────────────────────────────
void VoiceInputController::sendText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    SPDLOG_INFO("Sending text to foreground app: {}", text);
    pasteTextToActiveWindow(text, true, false);
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

void VoiceInputController::registerHotKey()
{
    if (!QHotkey::isPlatformSupported()) {
        SPDLOG_WARN("Global hotkeys are not supported on this platform");
        return;
    }

    m_hotkey =
        new QHotkey(QKeySequence(QStringLiteral("Ctrl+Alt+L")), true, this);
    connect(m_hotkey, &QHotkey::activated, this, [this]() {
        if (m_isListening) {
            stopListening();
        }
        else {
            startListening();
        }
    });

    if (!m_hotkey->isRegistered()) {
        SPDLOG_WARN("Failed to register global hotkey: Ctrl+Alt+L");
    }
    else {
        SPDLOG_INFO("Global hotkey registered: Ctrl+Alt+L");
    }
}

void VoiceInputController::unregisterHotKey()
{
    if (m_hotkey) {
        m_hotkey->setRegistered(false);
        delete m_hotkey;
        m_hotkey = nullptr;
    }
}

// ── SpeechRecognizer lifecycle ──────────────────────────────────

void VoiceInputController::loadModel(const nlohmann::json &preset)
{
    unloadModel();

    const QString modelDir = asrModelDir(preset);

    auto recognizer = SpeechRecognizer::createFromConfig(
        preset, modelDir, currentHotwordsConfig(), this);
    if (!recognizer) {
        SPDLOG_ERROR("VoiceInputController: model load failed: {}",
                     recognizer.error());
        emit modelLoadResult(false, recognizer.error());
        return;
    }

    connect(recognizer->get(), &SpeechRecognizer::resultChanged, this,
            &VoiceInputController::onResult);
    m_recognizer = std::move(*recognizer);

    // Persist providerId
    setCurrentAsrProviderId(jsonString(preset, "id"));

    emit modelLoadResult(true, {});
}

void VoiceInputController::unloadModel()
{
    if (m_recognizer && m_recognizer->isRunning()) {
        m_recognizer->stop();
    }
    m_recognizer.reset();
}

void VoiceInputController::startSession()
{
    if (m_recognizer) {
        m_recognizer->resetStream();
    }
}

void VoiceInputController::feedAudio(const QByteArray &pcm16, int sampleRate,
                                     int channels)
{
    if (m_recognizer) {
        m_recognizer->acceptPcm16(pcm16, sampleRate, channels);
    }
}

void VoiceInputController::finishSession()
{
    if (m_recognizer && m_recognizer->isRunning()) {
        m_recognizer->finish();
        if (m_recognizer->acceptsExternalAudio()) {
            m_recognizer->resetStream();
        }
    }
}

bool VoiceInputController::acceptsExternalAudio() const
{
    return !m_recognizer || m_recognizer->acceptsExternalAudio();
}

} // namespace talkinput
