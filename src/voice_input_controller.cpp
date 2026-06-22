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

// ── Pipeline ─────────────────────────────────────────────────────

QCoro::Task<void> VoiceInputController::executePipeline(PipelineMode mode)
{
    if (m_stage != PipelineStage::Idle) {
        STATUSBAR_INFO("{}", tr("Recognition is still processing."));
        co_return;
    }

    m_lastResult.clear();

    if (!m_recognizerSession->isSpeechRecognitionModelLoaded()) {
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
    m_recognizerSession->resetRecognitionStream();

    const bool external = m_recognizerSession->acceptsExternalAudio();
    if (external) {
        auto result = m_audioCapture->start();
        if (!result) {
            STATUSBAR_ERROR("{}", result.error());
            setStage(PipelineStage::Idle);
            co_return;
        }
    }

    // ── 2. Recognizing ────────────────────────────────────────────
    setStage(PipelineStage::Recognizing);

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
            SPDLOG_WARN("OCR context skipped: no focused screenshot");
        }
    }

    // ── 4. Optional: LLM Polishing ────────────────────────────────
    QString result;
    if (llmEnabled) {
        setStage(PipelineStage::Polishing);
        result = co_await m_llmPostProcessor->postProcess(
            trimmedText, ocrContext, currentHotwordsText());
    }
    else {
        result = trimmedText;
    }

    // ── 5. Commit ─────────────────────────────────────────────────
    {
        const QString committed = result.trimmed();
        if (!committed.isEmpty()) {
            if (m_textInjector->inject(committed)) {
                SPDLOG_INFO("VoiceInputController pasted final text");
            }
            else {
                SPDLOG_WARN("VoiceInputController failed to paste final text");
            }
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
        m_overlay->startAnimation();
        m_overlay->setPreviewText({});
        emit listeningChanged(true);
        break;
    case PipelineStage::Recognizing:
        SPDLOG_INFO("Pipeline stage → Recognizing");
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

    if (m_audioCapture) {
        m_audioCapture->stop();
    }

    if (!m_recognizerSession->finishRunningRecognitionStream()) {
        if (m_finalResultPromise && !m_finalResultPromise->isCanceled()) {
            m_finalResultPromise->addResult(QString());
            m_finalResultPromise->finish();
        }
        setStage(PipelineStage::Idle);
    }
}

// ── SpeechRecognizer lifecycle ──────────────────────────────────

void VoiceInputController::loadSpeechRecognitionModel(
    const nlohmann::json &preset)
{
    const auto result = m_recognizerSession->loadSpeechRecognitionModel(preset);
    if (!result) {
        STATUSBAR_ERROR("{}",
                        tr("Speech recognition model load failed: %1")
                            .arg(result.error()));
        return;
    }
}

void VoiceInputController::unloadSpeechRecognitionModel()
{
    m_recognizerSession->unloadSpeechRecognitionModel();
}

bool VoiceInputController::startSpeechRecognitionSession()
{
    if (m_stage != PipelineStage::Idle) {
        STATUSBAR_INFO("{}", tr("Recognition is still processing."));
        return false;
    }

    m_lastResult.clear();
    setStage(PipelineStage::Recognizing);

    if (!m_recognizerSession->isSpeechRecognitionModelLoaded()) {
        STATUSBAR_WARN("{}",
                       tr("Speech recognition model not loaded yet. Please "
                          "wait or select a model."));
        setStage(PipelineStage::Idle);
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
        setStage(PipelineStage::Idle);
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
