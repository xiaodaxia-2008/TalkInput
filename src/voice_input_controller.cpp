#include "voice_input_controller.h"
#include "app_config.h"
#include "llm_post_processor.h"
#include "logging.h"
#include "ocr_recognizer.h"
#include "platform_utils.h"
#include "speech_recognizer.h"
#include "utils.h"
#include "voice_hotkey.h"
#include "voice_overlay.h"

#include <QApplication>
#include <QCoro/QCoroFuture>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QPromise>
#include <QThread>

#include <ranges>

namespace
{

void saveOcrDebugImage(const QImage &image)
{
    if (image.isNull()) {
        return;
    }

    const QString dir = QDir(talkinput::appDataDir()).filePath("ocr_images");
    QDir().mkpath(dir);
    const QString timestamp =
        QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz");
    const QString path =
        QDir(dir).filePath(QString("ocr-context-%1.png").arg(timestamp));

    if (image.save(path, "PNG")) {
        SPDLOG_DEBUG("OCR context screenshot saved: {}", path);
    }
    else {
        SPDLOG_WARN("OCR context screenshot save failed: {}", path);
    }
}

QString hotwordsText()
{
    const auto &hotwords = talkinput::appConfig().settings.hotwords;
    return QString::fromStdString(hotwords | std::views::join_with('\n') |
                                  std::ranges::to<std::string>());
}

void saveAsrAudio(const QByteArray &pcm16, int sampleRate, int channels)
{
    if (pcm16.isEmpty()) {
        return;
    }

    const QString dir = QDir(talkinput::appDataDir()).filePath("asr_audios");
    QDir().mkpath(dir);
    const QString timestamp =
        QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz");
    const QString path =
        QDir(dir).filePath(QString("asr-%1.wav").arg(timestamp));

    const int dataSize = static_cast<int>(pcm16.size());
    const int fileSize = 36 + dataSize;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        SPDLOG_WARN("Failed to save ASR audio: {}", path);
        return;
    }

    const quint16 audioFormat = 1;
    const quint16 bitsPerSample = 16;
    const int byteRate = sampleRate * channels * bitsPerSample / 8;
    const quint16 blockAlign =
        static_cast<quint16>(channels * bitsPerSample / 8);

    auto write16 = [&](quint16 v) {
        file.write(reinterpret_cast<const char *>(&v), 2);
    };
    auto write32 = [&](quint32 v) {
        file.write(reinterpret_cast<const char *>(&v), 4);
    };

    file.write("RIFF", 4);
    write32(static_cast<quint32>(fileSize));
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    write32(16);
    write16(audioFormat);
    write16(static_cast<quint16>(channels));
    write32(static_cast<quint32>(sampleRate));
    write32(static_cast<quint32>(byteRate));
    write16(blockAlign);
    write16(bitsPerSample);
    file.write("data", 4);
    write32(static_cast<quint32>(dataSize));
    file.write(pcm16);

    SPDLOG_INFO("ASR audio saved: {} ({} bytes, {}Hz, {}ch)", path, dataSize,
                sampleRate, channels);
}

} // namespace

// ── VoiceInputController ─────────────────────────────────────────

namespace talkinput
{

QKeySequence hotkeySequence(PipelineMode mode)
{
    switch (mode) {
    case PipelineMode::AsrOnly:
        return QKeySequence(
            QString::fromStdString(appConfig().settings.asrHotKeys));
    case PipelineMode::AsrLlm:
        return QKeySequence(
            QString::fromStdString(appConfig().settings.asrLlmHotKeys));
    case PipelineMode::AsrLlmOcr:
        return QKeySequence(
            QString::fromStdString(appConfig().settings.asrLlmOcrHotKeys));
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
                    m_stage == PipelineStage::Recognizing)
                {
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
    const auto &config = appConfig();
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
    // For streaming models: onResult() now shows interim text during Recording.
    // For offline models: stopListening() transitions to Recognizing before
    // finish().

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

        const QImage image = m_ocrRecognizer->captureContextImage();
        if (!image.isNull()) {
            SPDLOG_INFO("OCR context screenshot captured: {}x{}", image.width(),
                        image.height());

            if (config.settings.saveOcrScreenshot) {
                saveOcrDebugImage(image);
            }

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
            trimmedText, ocrContext, hotwordsText());
    }
    else {
        result = trimmedText;
    }

    // ── 4b. Save ASR audio if enabled ───────────────────────────────
    if (config.settings.saveAsrAudio && m_recognizer) {
        const QByteArray audio = m_recognizer->takeCapturedAudio();
        if (!audio.isEmpty()) {
            const auto &fmt = m_recognizer->capturedAudioFormat();
            saveAsrAudio(audio, fmt.sampleRate(), fmt.channelCount());
        }
    }

    // ── 5. Commit ─────────────────────────────────────────────────
    {
        const QString committed = result.trimmed();
        if (!committed.isEmpty()) {
            // If the pipeline finished quickly (< pasteDelayMs since stop), the
            // foreground window may not have settled after the hotkey release.
            // Give it a brief pause before pasting.
            if (m_stopRequestedAt.hasExpired(config.settings.pasteDelayMs)) {
                SPDLOG_DEBUG("Pipeline took long enough (>={}ms), pasting "
                             "immediately",
                             config.settings.pasteDelayMs);
            }
            else {
                const auto wait =
                    config.settings.pasteDelayMs - m_stopRequestedAt.elapsed();
                SPDLOG_DEBUG("Pipeline finished quickly, waiting {}ms before "
                             "paste",
                             wait);
                QThread::msleep(static_cast<unsigned long>(wait));
            }
            pasteTextToActiveWindow(committed, config.settings.useClipboard,
                                    config.settings.copyToClipboard,
                                    config.settings.restoreClipboard);
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
        SPDLOG_DEBUG("Pipeline stage \u2192 Idle");
        m_overlay->stopAnimation();
        emit listeningChanged(false);
        break;
    case PipelineStage::Recording:
        SPDLOG_INFO("Pipeline stage \u2192 Recording");
        m_overlay->setIcon(QStringLiteral("\U0001f399"));
        m_overlay->startAnimation();
        m_overlay->setPreviewText(tr("Recording..."));
        emit listeningChanged(true);
        break;
    case PipelineStage::Recognizing:
        SPDLOG_INFO("Pipeline stage \u2192 Recognizing");
        m_overlay->setIcon(QStringLiteral("\U0001f50a"));
        m_overlay->setPreviewText(tr("Recognizing..."));
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

    // Show interim results during both Recording and Recognizing stages
    if ((m_stage == PipelineStage::Recording ||
         m_stage == PipelineStage::Recognizing) &&
        text != m_lastResult)
    {
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
    m_stopRequestedAt.start();
    SPDLOG_INFO("VoiceInputController: stop listening");

    if (!m_recognizer) {
        // No recognizer loaded — resolve empty and go idle
        if (m_finalResultPromise && !m_finalResultPromise->isCanceled()) {
            m_finalResultPromise->addResult(QString());
            m_finalResultPromise->finish();
        }
        setStage(PipelineStage::Idle);
        return;
    }

    m_recognizer->stopCapture();

    if (!m_recognizer->isRunning()) {
        // Recognizer not started (shouldn't happen during pipeline)
        if (m_finalResultPromise && !m_finalResultPromise->isCanceled()) {
            m_finalResultPromise->addResult(QString());
            m_finalResultPromise->finish();
        }
        setStage(PipelineStage::Idle);
        return;
    }

    // Transition to Recognizing stage BEFORE the blocking finish() call
    // so the overlay icon/text update is visible.
    setStage(PipelineStage::Recognizing);
    m_overlay->setPreviewText(tr("Recognizing..."));
    QApplication::processEvents();

    // finish() processes all accumulated audio.
    // For streaming models this is fast; for offline models it decodes
    // all audio synchronously (may block briefly for long audio).
    m_recognizer->finish();

    // finish() should have emitted resultChanged(isFinal=true) which
    // resolved the promise via onResult(). If not, resolve with empty
    // to prevent the pipeline from hanging.
    if (m_finalResultPromise && !m_finalResultPromise->isCanceled() &&
        !m_finalResultPromise->future().isFinished())
    {
        m_finalResultPromise->addResult(QString());
        m_finalResultPromise->finish();
        setStage(PipelineStage::Idle);
    }
}

// ── SpeechRecognizer lifecycle ──────────────────────────────────

void VoiceInputController::loadSpeechRecognitionModel(const AsrPreset &preset)
{
    unloadSpeechRecognitionModel();

    auto recognizer = SpeechRecognizer::createFromPreset(preset, this);
    if (!recognizer) {
        STATUSBAR_ERROR("{}", tr("Speech recognition model load failed: %1")
                                  .arg(recognizer.error()));
        return;
    }

    connect(recognizer->get(), &SpeechRecognizer::resultChanged, this,
            &VoiceInputController::onResult);
    m_recognizer = std::move(*recognizer);

    appConfig().settings.asrProviderId = preset.id;
    markConfigDirty();
}

void VoiceInputController::reloadOcrRecognizer()
{
    const auto &presets = appConfig().ocrPresets;
    const auto it = presets.find(appConfig().settings.ocrProviderId);
    if (it == presets.end()) {
        SPDLOG_WARN("reloadOcrRecognizer: unknown OCR provider '{}'",
                    appConfig().settings.ocrProviderId);
        m_ocrRecognizer.reset();
        return;
    }

    auto ocr = OcrRecognizer::createFromPreset(it->second);
    if (!ocr) {
        SPDLOG_WARN("reloadOcrRecognizer: failed to create: {}", ocr.error());
        m_ocrRecognizer.reset();
        return;
    }

    m_ocrRecognizer = std::move(*ocr);
    SPDLOG_INFO("OCR provider reloaded: {}", it->second.name);
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
    return m_recognizer ? m_recognizer->preset().id : std::string();
}

} // namespace talkinput
