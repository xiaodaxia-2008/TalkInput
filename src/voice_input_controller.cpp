#include "voice_input_controller.h"
#include "app_config.h"
#include "llm_post_processor.h"
#include "logging.h"
#include "ocr_recognizer.h"
#include "paste_text.h"
#include "speech_recognizer.h"
#include "utils.h"
#include "voice_hotkey.h"
#include "voice_overlay.h"

#include <QCoro/QCoroFuture>
#include <QDateTime>
#include <QDir>
#include <QPromise>

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

} // namespace

// ── VoiceInputController ─────────────────────────────────────────

namespace talkinput
{

QKeySequence hotkeySequence(PipelineMode mode)
{
    switch (mode) {
    case PipelineMode::AsrOnly:
        return QKeySequence(QString::fromStdString(
            appConfig().settings.asrHotKeys));
    case PipelineMode::AsrLlm:
        return QKeySequence(QString::fromStdString(
            appConfig().settings.asrLlmHotKeys));
    case PipelineMode::AsrLlmOcr:
        return QKeySequence(QString::fromStdString(
            appConfig().settings.asrLlmOcrHotKeys));
    }
    return {};
}

void setHotkeySequence(PipelineMode mode, const QKeySequence &keys)
{
    const std::string value = keys.toString().toStdString();
    switch (mode) {
    case PipelineMode::AsrOnly:
        appConfig().settings.asrHotKeys = value;
        break;
    case PipelineMode::AsrLlm:
        appConfig().settings.asrLlmHotKeys = value;
        break;
    case PipelineMode::AsrLlmOcr:
        appConfig().settings.asrLlmOcrHotKeys = value;
        break;
    }
    markConfigDirty();
}

QString hotkeyConfigPath(PipelineMode mode)
{
    return {};
}

static VoiceInputController *s_instance = nullptr;

VoiceInputController *VoiceInputController::instance()
{
    return s_instance;
}

void VoiceInputController::reregisterHotkey(PipelineMode mode)
{
    if (m_hotkey) {
        m_hotkey->reregisterShortcut(mode);
    }
}

VoiceInputController::VoiceInputController(QObject *parent) : QObject(parent)
{
    s_instance = this;

    m_hotkey = std::make_unique<VoiceHotkey>();
    connect(m_hotkey.get(), &VoiceHotkey::activated, this,
            [this](PipelineMode mode) {
                if (m_stage == PipelineStage::Recording ||
                    m_stage == PipelineStage::Recognizing) {
                    stopListening();
                }
                else if (m_stage == PipelineStage::Idle) {
                    m_pipelineMode = mode;
                    startListening();
                }
            });

    m_overlay = std::make_unique<VoiceOverlay>();
    m_llmPostProcessor = std::make_unique<LlmPostProcessor>();
    auto ocr = OcrRecognizer::createFromPreset(
        appConfig().ocrPresets.at(appConfig().settings.ocrProviderId));
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

// ── Pipeline ─────────────────────────────────────────────────────

QCoro::Task<void> VoiceInputController::executePipeline(PipelineMode mode)
{
    if (m_stage != PipelineStage::Idle) {
        STATUSBAR_INFO("{}", tr("Recognition is still processing."));
        co_return;
    }

    m_lastResult.clear();

    if (!m_recognizer) {
        STATUSBAR_WARN("{}",
                       tr("Speech recognition model not loaded yet. Please "
                          "wait or select a model."));
        co_return;
    }

    const bool ocrEnabled = mode == PipelineMode::AsrLlmOcr;
    const bool llmEnabled = mode != PipelineMode::AsrOnly;

    SPDLOG_INFO("executePipeline: mode={} ocrEnabled={} llmEnabled={}",
                static_cast<int>(mode), ocrEnabled, llmEnabled);

    // ── 1. Recording ──────────────────────────────────────────────
    setStage(PipelineStage::Recording);
    m_recognizer->resetStream();

    {
        auto result = m_recognizer->startCapture();
        if (!result) {
            STATUSBAR_ERROR("{}", result.error());
            setStage(PipelineStage::Idle);
            co_return;
        }
    }

    // Stage stays Recording while capturing audio.
    // onResult() transitions to Recognizing when results arrive.

    QPromise<QString> resultPromise;
    resultPromise.start();
    auto future = resultPromise.future();
    m_finalResultPromise = &resultPromise;

    const QString finalText = co_await future;
    m_finalResultPromise = nullptr;

    const QString trimmedText = finalText.trimmed();
    if (trimmedText.isEmpty()) {
        SPDLOG_WARN("executePipeline: empty final text, aborting");
        setStage(PipelineStage::Idle);
        co_return;
    }

    // ── 3. Optional: OCR ──────────────────────────────────────────
    QString ocrContext;
    if (ocrEnabled && m_ocrRecognizer && m_ocrRecognizer->isAvailable()) {
        setStage(PipelineStage::ReadingContext);

        const QImage image = m_ocrRecognizer->captureFocusedTextInputImage();
        if (!image.isNull()) {
            SPDLOG_INFO("OCR context screenshot captured: {}x{}",
                        image.width(), image.height());

            ocrContext = co_await m_ocrRecognizer->recognizeText(image);
            SPDLOG_INFO("OCR context result received: {}", ocrContext);
        }
        else {
            SPDLOG_WARN("OCR context skipped: no focused screenshot");
        }
    }

    // ── 4. Optional: LLM Polishing ────────────────────────────────
    QString result;
    if (llmEnabled) {
        setStage(PipelineStage::Polishing);
        result = co_await m_llmPostProcessor->postProcess(
            trimmedText, ocrContext,
            QString::fromStdString([&]() {
                QStringList lines;
                for (const auto &item : appConfig().settings.hotwords) {
                    const QString line =
                        QString::fromStdString(item).trimmed();
                    if (!line.isEmpty()) {
                        lines.append(line);
                    }
                }
                return lines.join(QLatin1Char('\n')).toStdString();
            }()));
    }
    else {
        result = trimmedText;
    }

    // ── 5. Commit ─────────────────────────────────────────────────
    {
        const QString committed = result.trimmed();
        if (!committed.isEmpty()) {
            pasteTextToActiveWindow(committed,
                                    appConfig().settings.useClipboard,
                                    appConfig().settings.copyToClipboard,
                                    appConfig().settings.restoreClipboard);
            SPDLOG_INFO("VoiceInputController pasted final text");
            emit finalTextCommitted(committed);
            SPDLOG_INFO("VoiceInputController saved final text to history: {}",
                        committed);
        }
    }

    setStage(PipelineStage::Idle);
}

void VoiceInputController::setStage(PipelineStage stage)
{
    if (m_stage == stage) {
        return;
    }
    m_stage = stage;

    switch (stage) {
    case PipelineStage::Idle:
        SPDLOG_DEBUG("Pipeline stage → Idle");
        m_overlay->stopAnimation();
        emit listeningChanged(false);
        break;
    case PipelineStage::Recording:
        SPDLOG_INFO("Pipeline stage → Recording");
        m_overlay->setIcon(QStringLiteral("🎙"));
        m_overlay->startAnimation();
        m_overlay->setPreviewText(tr("Recording..."));
        emit listeningChanged(true);
        break;
    case PipelineStage::Recognizing:
        SPDLOG_INFO("Pipeline stage → Recognizing");
        m_overlay->setIcon(QStringLiteral("🔊"));
        emit listeningChanged(true);
        break;
    case PipelineStage::ReadingContext:
        SPDLOG_INFO("Pipeline stage → ReadingContext");
        m_overlay->setIcon(QStringLiteral("📄"));
        m_overlay->setPreviewText(tr("Reading focused input context..."));
        emit listeningChanged(false);
        break;
    case PipelineStage::Polishing:
        SPDLOG_INFO("Pipeline stage → Polishing");
        m_overlay->setIcon(QStringLiteral("✨"));
        m_overlay->setPreviewText(tr("Post-processing recognition result..."));
        emit listeningChanged(false);
        break;
    }
}

// ── Result callback ───────────────────────────────────────────────

void VoiceInputController::onResult(const QString &text, bool isFinal)
{
    if (isFinal) {
        if (m_finalResultPromise && !m_finalResultPromise->isCanceled()) {
            m_finalResultPromise->addResult(text);
            m_finalResultPromise->finish();
            return;
        }
        emit finalTextCommitted(text.trimmed());
        return;
    }

    if (m_stage == PipelineStage::Recognizing && text != m_lastResult) {
        m_lastResult = text;
        if (m_overlay) {
            m_overlay->setPreviewText(text);
        }
    }
}

// ── Public API: external control ──────────────────────────────────

bool VoiceInputController::startListening()
{
    if (m_stage != PipelineStage::Idle) {
        STATUSBAR_INFO("{}", tr("Recognition is still processing."));
        return false;
    }
    executePipeline(m_pipelineMode);
    return true;
}

void VoiceInputController::stopListening()
{
    SPDLOG_INFO("VoiceInputController: stop listening");

    if (m_recognizer) {
        m_recognizer->stopCapture();
    }

    if (!m_recognizer || !m_recognizer->isRunning()) {
        if (m_finalResultPromise && !m_finalResultPromise->isCanceled()) {
            m_finalResultPromise->addResult(QString());
            m_finalResultPromise->finish();
        }
        setStage(PipelineStage::Idle);
    }
    else {
        setStage(PipelineStage::Recognizing);
        m_recognizer->finish();
        // finish() is synchronous — recognition is complete after it returns.
        // If the recognizer already emitted resultChanged(isFinal=true),
        // onResult already resolved the promise; otherwise resolve it now
        // with empty text so the pipeline doesn't hang forever.
        // Only force Idle when the promise was NOT already resolved by
        // a real recognition result — otherwise let the pipeline continue.
        if (m_finalResultPromise && !m_finalResultPromise->isCanceled() &&
            !m_finalResultPromise->future().isFinished())
        {
            m_finalResultPromise->addResult(QString());
            m_finalResultPromise->finish();
            setStage(PipelineStage::Idle);
        }
    }
}

// ── SpeechRecognizer lifecycle ──────────────────────────────────

void VoiceInputController::loadSpeechRecognitionModel(
    const AsrPreset &preset)
{
    unloadSpeechRecognitionModel();

    auto recognizer = SpeechRecognizer::createFromPreset(preset, this);
    if (!recognizer) {
        STATUSBAR_ERROR("{}",
                        tr("Speech recognition model load failed: %1")
                            .arg(recognizer.error()));
        return;
    }

    connect(recognizer->get(), &SpeechRecognizer::resultChanged, this,
            &VoiceInputController::onResult);
    m_recognizer = std::move(*recognizer);

    appConfig().settings.asrProviderId = preset.id;
    markConfigDirty();
}

void VoiceInputController::unloadSpeechRecognitionModel()
{
    if (m_recognizer && m_recognizer->isRunning()) {
        m_recognizer->stop();
    }
    m_recognizer.reset();
}

bool VoiceInputController::startSpeechRecognitionSession()
{
    if (m_stage != PipelineStage::Idle) {
        STATUSBAR_INFO("{}", tr("Recognition is still processing."));
        return false;
    }

    m_lastResult.clear();
    setStage(PipelineStage::Recognizing);

    if (!m_recognizer) {
        STATUSBAR_WARN("{}",
                       tr("Speech recognition model not loaded yet. Please "
                          "wait or select a model."));
        setStage(PipelineStage::Idle);
        return false;
    }

    m_recognizer->resetStream();
    return true;
}

void VoiceInputController::feedSpeechRecognitionAudio(const QByteArray &pcm16,
                                                      int sampleRate,
                                                      int channels)
{
    if (m_recognizer) {
        m_recognizer->acceptPcm16(pcm16, sampleRate, channels);
    }
}

void VoiceInputController::finishSpeechRecognitionSession()
{
    if (!m_recognizer || !m_recognizer->isRunning()) {
        setStage(PipelineStage::Idle);
        return;
    }

    m_recognizer->finish();
    m_recognizer->resetStream();
    setStage(PipelineStage::Idle);
}

bool VoiceInputController::isSpeechRecognitionModelLoaded() const
{
    return m_recognizer != nullptr;
}

SpeechRecognizer *VoiceInputController::speechRecognizer() const
{
    return m_recognizer.get();
}

std::string VoiceInputController::loadedPresetId() const
{
    return m_recognizer ? m_recognizer->presetId() : std::string();
}

} // namespace talkinput
