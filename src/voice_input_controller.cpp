#include "voice_input_controller.h"
#include "app_config.h"
#include "asr_config.h"
#include "audio_input_capture.h"
#include "llm_post_processor.h"
#include "logging.h"
#include "ocr_config.h"
#include "ocr_recognizer.h"
#include "text_injector.h"
#include "utils.h"
#include "voice_hotkey.h"
#include "voice_overlay.h"
#include "voice_recognizer_session.h"

#include <QCoro/QCoroFuture>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>

namespace
{

void saveOcrDebugImage(const QImage &image)
{
    if (image.isNull()) {
        return;
    }

    const QString dir = QDir(talkinput::appDataDir()).filePath("ocr");
    QDir().mkpath(dir);
    const QString timestamp =
        QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz");
    const QString path =
        QDir(dir).filePath(QString("ocr-context-%1.png").arg(timestamp));
    const QString latestPath = QDir(dir).filePath("ocr-context-latest.png");

    if (image.save(path, "PNG")) {
        image.save(latestPath, "PNG");
        SPDLOG_DEBUG("OCR context debug screenshot saved: {}", path);
    }
    else {
        SPDLOG_WARN("OCR context debug screenshot save failed: {}", path);
    }
}

QScreen *screenByName(const QString &name)
{
    if (name.isEmpty()) {
        return nullptr;
    }

    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen && screen->name().compare(name, Qt::CaseInsensitive) == 0) {
            return screen;
        }
    }
    return nullptr;
}

} // namespace

// ── VoiceInputController ─────────────────────────────────────────

namespace talkinput
{

QKeySequence hotkeySequence(PipelineMode mode)
{
    return QKeySequence(
        appConfigString(hotkeyConfigPath(mode).toStdString()));
}

void setHotkeySequence(PipelineMode mode, const QKeySequence &keys)
{
    setAppConfigValue(hotkeyConfigPath(mode).toStdString(),
                      keys.toString().toStdString());
}

QString hotkeyConfigPath(PipelineMode mode)
{
    switch (mode) {
    case PipelineMode::AsrOnly:
        return QStringLiteral("/settings/hotkeys/asr");
    case PipelineMode::AsrLlm:
        return QStringLiteral("/settings/hotkeys/asrLlm");
    case PipelineMode::AsrLlmOcr:
        return QStringLiteral("/settings/hotkeys/asrLlmOcr");
    }
    return {};
}

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
    connect(m_hotkey.get(), &VoiceHotkey::activated, this,
            [this](PipelineMode mode) {
                if (m_isListening) {
                    stopListening();
                }
                else {
                    startRecording(FinalTextAction::PasteAndRecordHistory,
                                   mode);
                }
            });

    m_overlay = std::make_unique<VoiceOverlay>();
    m_textInjector = std::make_unique<TextInjector>();
    m_llmPostProcessor = std::make_unique<LlmPostProcessor>();
    auto ocr = OcrRecognizer::createFromConfig(currentOcrPreset());
    if (!ocr) {
        SPDLOG_WARN("OcrRecognizer: failed to create: {}", ocr.error());
    }
    else {
        m_ocrRecognizer = std::move(*ocr);
    }
}

VoiceInputController::~VoiceInputController()
{
    stopListening();
    s_instance = nullptr;
}

bool VoiceInputController::startListening()
{
    return startRecording(FinalTextAction::RecordHistoryOnly);
}

bool VoiceInputController::startRecording(FinalTextAction finalTextAction,
                                         PipelineMode pipelineMode)
{
    m_pipelineMode = pipelineMode;
    SPDLOG_INFO("VoiceInputController: start listening (mode {})",
                static_cast<int>(pipelineMode));

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

QCoro::Task<void> VoiceInputController::postProcessFinalText(
    const QString &text, FinalTextAction finalTextAction)
{
    const QString finalText = text.trimmed();
    if (finalText.isEmpty()) {
        commitFinalText(text, finalTextAction);
        co_return;
    }

    const bool llmEnabled = m_pipelineMode != PipelineMode::AsrOnly;
    const bool ocrEnabled = m_pipelineMode == PipelineMode::AsrLlmOcr;

    SPDLOG_INFO("VoiceInputController processing final text: mode={} "
                "llmEnabled={} ocrEnabled={}",
                static_cast<int>(m_pipelineMode), llmEnabled, ocrEnabled);

    if (!llmEnabled) {
        SPDLOG_INFO("LLM post-processing skipped: ASR-only mode");
        commitFinalText(finalText, finalTextAction);
        co_return;
    }

    QString ocrContext;
    if (ocrEnabled && m_ocrRecognizer && m_ocrRecognizer->isAvailable()) {
        const QImage image = captureFocusedContextImage();
        if (!image.isNull()) {
            SPDLOG_INFO("OCR context screenshot captured: {}x{}",
                        image.width(), image.height());
            STATUSBAR_INFO("{}", tr("Reading focused input context..."));

            QPromise<QString> ocrPromise;
            ocrPromise.start();
            auto ocrFuture = ocrPromise.future();
            m_ocrRecognizer->recognizeText(
                image, this,
                [&ocrPromise](const QString &contextText) mutable {
                    if (!ocrPromise.isCanceled()) {
                        ocrPromise.addResult(contextText.trimmed());
                        ocrPromise.finish();
                    }
                });
            ocrContext = co_await ocrFuture;
            SPDLOG_INFO("OCR context result received: {}", ocrContext);
        }
        else {
            SPDLOG_INFO("OCR context skipped: no focused screenshot");
        }
    }
    else {
        SPDLOG_INFO("OCR context skipped: {}",
                     ocrEnabled ? "service unavailable" : "ASR+LLM mode");
    }

    STATUSBAR_INFO("{}", tr("Post-processing recognition result..."));
    const QString result = co_await m_llmPostProcessor->postProcess(
        finalText, ocrContext, currentHotwordsText());

    commitFinalText(result.trimmed(), finalTextAction);
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

QImage VoiceInputController::captureFocusedContextImage()
{
    if (m_ocrRecognizer) {
        const QImage focusedWindowImage =
            m_ocrRecognizer->captureFocusedTextInputImage();
        if (!focusedWindowImage.isNull()) {
            SPDLOG_DEBUG("OCR context focused window screenshot captured: "
                         "{}x{}",
                         focusedWindowImage.width(),
                         focusedWindowImage.height());
            saveOcrDebugImage(focusedWindowImage);
            return focusedWindowImage;
        }
        SPDLOG_DEBUG("OCR context focused window screenshot failed; falling "
                     "back to full screen");
    }

    const QString screenName =
        m_ocrRecognizer ? m_ocrRecognizer->focusedTextInputScreenName()
                        : QString();
    QScreen *screen = screenByName(screenName);
    if (screen) {
        SPDLOG_DEBUG("OCR context matched focused screen '{}'", screen->name());
    }
    if (!screen) {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        SPDLOG_DEBUG("OCR context screenshot skipped: no screen");
        return {};
    }

    const QPixmap pixmap = screen->grabWindow(0);
    const QImage image = pixmap.toImage();
    SPDLOG_DEBUG("OCR context using full-screen fallback on screen '{}': "
                 "{}x{} dpr={}",
                 screen->name(), pixmap.width(), pixmap.height(),
                 pixmap.devicePixelRatio());
    return image;
}

} // namespace talkinput
