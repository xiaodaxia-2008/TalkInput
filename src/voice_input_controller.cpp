#include "voice_input_controller.h"
#include "app_config.h"
#include "audio_utils.h"
#include "llm_post_processor.h"
#include "logging.h"
#include "ocr_recognizer.h"
#include "platform_utils.h"
#include "speech_recognizer.h"
#include "utils.h"
#include "voice_hotkey.h"
#include "voice_overlay.h"

#include <QApplication>
#include <QAudioDevice>
#include <QAudioSource>
#include <QCoro/QCoroFuture>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QMediaDevices>
#include <QMetaObject>
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

    talkinput::savePcm16ToWav(pcm16, sampleRate, channels, path);
}

} // namespace

// ── VoiceInputController ─────────────────────────────────────────

namespace talkinput
{

PipelineMode pipelineModeFromString(const std::string &s)
{
    if (s == "asr_only") {
        return PipelineMode::AsrOnly;
    }
    if (s == "asr_llm") {
        return PipelineMode::AsrLlm;
    }
    if (s == "asr_llm_ocr") {
        return PipelineMode::AsrLlmOcr;
    }
    return PipelineMode::AsrLlmOcr;
}

std::string pipelineModeToString(PipelineMode mode)
{
    switch (mode) {
    case PipelineMode::AsrOnly:
        return "asr_only";
    case PipelineMode::AsrLlm:
        return "asr_llm";
    case PipelineMode::AsrLlmOcr:
        return "asr_llm_ocr";
    }
    return "asr_llm_ocr";
}

QString pipelineModeDisplayName(PipelineMode mode)
{
    switch (mode) {
    case PipelineMode::AsrOnly:
        return QStringLiteral("🎙");
    case PipelineMode::AsrLlm:
        return QStringLiteral("🎙✨");
    case PipelineMode::AsrLlmOcr:
        return QStringLiteral("🎙✨📄");
    }
    return {};
}

static VoiceInputController *s_instance = nullptr;

VoiceInputController *VoiceInputController::instance()
{
    return s_instance;
}

void VoiceInputController::reregisterTriggerHotkey()
{
    if (m_hotkey) {
        m_hotkey->reregisterTriggerShortcut();
    }
}

void VoiceInputController::reregisterModeSwitchHotkey()
{
    if (m_hotkey) {
        m_hotkey->reregisterModeSwitchShortcut();
    }
}

void VoiceInputController::cyclePipelineMode()
{
    switch (m_pipelineMode) {
    case PipelineMode::AsrOnly:
        m_pipelineMode = PipelineMode::AsrLlm;
        break;
    case PipelineMode::AsrLlm:
        m_pipelineMode = PipelineMode::AsrLlmOcr;
        break;
    case PipelineMode::AsrLlmOcr:
        m_pipelineMode = PipelineMode::AsrOnly;
        break;
    }
    appConfig().settings.activeMode = pipelineModeToString(m_pipelineMode);
    markConfigDirty();
    emit modeChanged(m_pipelineMode);
}

VoiceInputController::VoiceInputController(QObject *parent) : QObject(parent)
{
    s_instance = this;

    m_pipelineMode = pipelineModeFromString(appConfig().settings.activeMode);

    m_hotkey = std::make_unique<VoiceHotkey>();

    // Unified trigger: start/stop voice input with current active mode
    connect(m_hotkey.get(), &VoiceHotkey::triggerActivated, this, [this]() {
        if (m_stage == PipelineStage::Recording ||
            m_stage == PipelineStage::Recognizing)
        {
            stopListening();
        }
        else if (m_stage == PipelineStage::Idle) {
            m_pipelineMode =
                pipelineModeFromString(appConfig().settings.activeMode);
            startListening();
        }
    });

    // Mode switch: cycle mode — works globally at any time, no popup
    connect(m_hotkey.get(), &VoiceHotkey::modeSwitchActivated, this, [this]() {
        cyclePipelineMode();

        // Update mode text in overlay if recording
        if (m_overlay && m_overlay->isVisible()) {
            m_overlay->setModeText(pipelineModeDisplayName(m_pipelineMode));
        }

        SPDLOG_INFO("Pipeline mode switched to: {}",
                    pipelineModeToString(m_pipelineMode));
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
    unloadSpeechRecognitionModel();
    s_instance = nullptr;
}

// ── Pipeline ─────────────────────────────────────────────────────

QCoro::Task<void> VoiceInputController::executePipeline()
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

    SPDLOG_INFO("executePipeline: mode={}", static_cast<int>(m_pipelineMode));

    // ── 1. Recording ──────────────────────────────────────────────
    setStage(PipelineStage::Recording);
    queueRecognizerReset();

    {
        auto result = startAudioCapture();
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
    const bool ocrEnabled = m_pipelineMode == PipelineMode::AsrLlmOcr;
    if (ocrEnabled && m_ocrRecognizer && m_ocrRecognizer->isAvailable()) {
        setStage(PipelineStage::ReadingContext);

        const QImage image = m_ocrRecognizer->captureContextImage();
        if (!image.isNull()) {
            if (config.settings.saveOcrScreenshot) {
                saveOcrDebugImage(image);
            }
            ocrContext = co_await m_ocrRecognizer->recognizeText(image);
        }
        else {
            SPDLOG_WARN("OCR context skipped: no focused screenshot");
        }
    }

    // ── 4. Optional: LLM Polishing ────────────────────────────────
    QString result;
    const bool llmEnabled = m_pipelineMode != PipelineMode::AsrOnly;
    if (llmEnabled) {
        setStage(PipelineStage::Polishing);
        result = co_await m_llmPostProcessor->postProcess(
            trimmedText, ocrContext, hotwordsText());
    }
    else {
        result = trimmedText;
    }

    // ── 4b. Save ASR audio if enabled ───────────────────────────────
    if (config.settings.saveAsrAudio) {
        const QByteArray audio = std::move(m_capturedAudio);
        if (!audio.isEmpty() && m_audioFormat.isValid()) {
            saveAsrAudio(audio, m_audioFormat.sampleRate(),
                         m_audioFormat.channelCount());
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
        SPDLOG_DEBUG("Pipeline stage → Idle");
        m_overlay->stopAnimation();
        emit listeningChanged(false);
        break;
    case PipelineStage::Recording:
        SPDLOG_INFO("Pipeline stage → Recording");
        m_overlay->setModeText(pipelineModeDisplayName(m_pipelineMode));
        m_overlay->startAnimation();
        m_overlay->setPreviewText(tr("Recording..."));
        emit listeningChanged(true);
        break;
    case PipelineStage::Recognizing:
        SPDLOG_INFO("Pipeline stage → Recognizing");
        m_overlay->setPreviewText(tr("Recognizing..."));
        emit listeningChanged(true);
        break;
    case PipelineStage::ReadingContext:
        SPDLOG_INFO("Pipeline stage → ReadingContext");
        m_overlay->setPreviewText(tr("Reading focused input context..."));
        emit listeningChanged(false);
        break;
    case PipelineStage::Polishing:
        SPDLOG_INFO("Pipeline stage → Polishing");
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
        setStage(PipelineStage::Idle);
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
    executePipeline();
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

    stopAudioCapture();

    // Transition to Recognizing stage BEFORE the blocking finish() call
    // so the overlay icon/text update is visible.
    setStage(PipelineStage::Recognizing);
    m_overlay->setPreviewText(tr("Recognizing..."));
    QApplication::processEvents();

    // finish() is queued onto the recognizer thread. The pipeline promise is
    // resolved when resultChanged(isFinal=true) returns from that thread.
    queueRecognizerFinish();
}

// ── Audio capture ────────────────────────────────────────────────

std::expected<void, QString> VoiceInputController::startAudioCapture()
{
    if (m_audioSource) {
        return {};
    }

    m_capturedAudio.clear();

    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        SPDLOG_ERROR("No audio input device");
        return std::unexpected(tr("No microphone available"));
    }

    m_audioFormat = inputDevice.preferredFormat();
    if (!m_audioFormat.isValid() ||
        m_audioFormat.sampleFormat() == QAudioFormat::Unknown)
    {
        m_audioFormat = QAudioFormat();
        m_audioFormat.setSampleRate(48000);
        m_audioFormat.setChannelCount(1);
        m_audioFormat.setSampleFormat(QAudioFormat::Int16);
    }

    if (!inputDevice.isFormatSupported(m_audioFormat)) {
        SPDLOG_ERROR("Audio format not supported");
        return std::unexpected(tr("Microphone format not supported."));
    }

    m_audioSource = std::make_unique<QAudioSource>(inputDevice, m_audioFormat);
    m_audioDevice = m_audioSource->start();
    if (!m_audioDevice) {
        SPDLOG_ERROR("Failed to start microphone");
        m_audioSource.reset();
        return std::unexpected(tr("Failed to start microphone"));
    }

    connect(m_audioDevice, &QIODevice::readyRead, this, [this]() {
        if (!m_audioDevice) {
            return;
        }

        const QByteArray audioData = m_audioDevice->readAll();
        const QByteArray pcm16 = convertAudioToPcm16(audioData, m_audioFormat);
        if (pcm16.isEmpty()) {
            return;
        }

        m_capturedAudio.append(pcm16);
        queueRecognizerAudio(pcm16, m_audioFormat.sampleRate(),
                             m_audioFormat.channelCount());
    });

    return {};
}

void VoiceInputController::stopAudioCapture()
{
    if (m_audioSource) {
        m_audioSource->stop();
    }
    m_audioDevice = nullptr;
    m_audioSource.reset();
}

bool VoiceInputController::isAudioCaptureRunning() const
{
    return m_audioSource != nullptr;
}

// ── Recognizer worker dispatch ───────────────────────────────────

void VoiceInputController::queueRecognizerReset()
{
    if (!m_recognizer) {
        return;
    }

    auto *recognizer = m_recognizer;
    QMetaObject::invokeMethod(
        recognizer, [recognizer]() { recognizer->resetStream(); },
        Qt::QueuedConnection);
}

void VoiceInputController::queueRecognizerAudio(const QByteArray &pcm16,
                                                int sampleRate, int channels)
{
    if (!m_recognizer) {
        return;
    }

    auto *recognizer = m_recognizer;
    QMetaObject::invokeMethod(
        recognizer,
        [recognizer, pcm16, sampleRate, channels]() {
            recognizer->acceptPcm16(pcm16, sampleRate, channels);
        },
        Qt::QueuedConnection);
}

void VoiceInputController::queueRecognizerFinish()
{
    if (!m_recognizer) {
        return;
    }

    auto *recognizer = m_recognizer;
    QMetaObject::invokeMethod(
        recognizer, [recognizer]() { recognizer->finish(); },
        Qt::QueuedConnection);
}

// ── SpeechRecognizer lifecycle ──────────────────────────────────

void VoiceInputController::loadSpeechRecognitionModel(const AsrPreset &preset)
{
    unloadSpeechRecognitionModel();

    auto recognizer =
        SpeechRecognizer::createFromPreset(preset, nullptr, false);
    if (!recognizer) {
        STATUSBAR_ERROR("{}", tr("Speech recognition model load failed: %1")
                                  .arg(recognizer.error()));
        return;
    }

    m_recognizerThread = std::make_unique<QThread>();
    m_recognizer = recognizer->release();
    m_recognizer->moveToThread(m_recognizerThread.get());

    connect(m_recognizer, &SpeechRecognizer::resultChanged, this,
            &VoiceInputController::onResult);

    m_recognizerThread->start();

    std::expected<void, QString> startResult;
    const bool invoked = QMetaObject::invokeMethod(
        m_recognizer,
        [this, &startResult]() { startResult = m_recognizer->start(); },
        Qt::BlockingQueuedConnection);
    if (!invoked || !startResult) {
        const QString error =
            invoked ? startResult.error()
                    : QStringLiteral("Failed to start recognizer thread.");
        unloadSpeechRecognitionModel();
        STATUSBAR_ERROR(
            "{}", tr("Speech recognition model load failed: %1").arg(error));
        return;
    }

    appConfig().settings.asrProviderId = preset.id;
    m_loadedPresetId = preset.id;
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
    stopAudioCapture();

    if (m_recognizer) {
        auto *recognizer = m_recognizer;
        QThread *thread = m_recognizerThread.get();
        if (thread && thread->isRunning()) {
            connect(recognizer, &QObject::destroyed, thread, &QThread::quit);
            QMetaObject::invokeMethod(
                recognizer,
                [recognizer]() {
                    recognizer->stop();
                    recognizer->deleteLater();
                },
                Qt::QueuedConnection);
            thread->wait();
        }
        else {
            delete recognizer;
        }
        m_recognizer = nullptr;
    }

    if (m_recognizerThread) {
        if (m_recognizerThread->isRunning()) {
            m_recognizerThread->quit();
            m_recognizerThread->wait();
        }
        m_recognizerThread.reset();
    }
    m_loadedPresetId.clear();
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

    queueRecognizerReset();
    return true;
}

void VoiceInputController::feedSpeechRecognitionAudio(const QByteArray &pcm16,
                                                      int sampleRate,
                                                      int channels)
{
    if (m_recognizer) {
        queueRecognizerAudio(pcm16, sampleRate, channels);
    }
}

void VoiceInputController::finishSpeechRecognitionSession()
{
    if (!m_recognizer) {
        setStage(PipelineStage::Idle);
        return;
    }

    queueRecognizerFinish();
}

bool VoiceInputController::isSpeechRecognitionModelLoaded() const
{
    return m_recognizer != nullptr;
}

SpeechRecognizer *VoiceInputController::speechRecognizer() const
{
    return m_recognizer;
}

std::string VoiceInputController::loadedPresetId() const
{
    return m_loadedPresetId;
}

} // namespace talkinput
