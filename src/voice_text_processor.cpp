#include "voice_text_processor.h"
#include "asr_config.h"
#include "llm_post_processor.h"
#include "logging.h"
#include "ocr_config.h"
#include "ocr_recognizer.h"
#include "utils.h"

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

namespace talkinput
{

VoiceTextProcessor::VoiceTextProcessor(QObject *parent) : QObject(parent)
{
    m_llmPostProcessor = std::make_unique<LlmPostProcessor>();
    auto ocr = OcrRecognizer::createFromConfig(currentOcrPreset());
    if (!ocr) {
        SPDLOG_WARN("OcrRecognizer: failed to create: {}", ocr.error());
    }
    else {
        m_ocrRecognizer = std::move(*ocr);
    }
}

VoiceTextProcessor::~VoiceTextProcessor() = default;

QCoro::Task<QString> VoiceTextProcessor::processFinalText(
    const QString &text, PipelineMode pipelineMode)
{
    const QString finalText = text.trimmed();
    if (finalText.isEmpty()) {
        co_return text;
    }

    const bool llmEnabled = pipelineMode != PipelineMode::AsrOnly;
    const bool ocrEnabled = pipelineMode == PipelineMode::AsrLlmOcr;

    SPDLOG_INFO("VoiceTextProcessor received final text: mode={} llmEnabled={} "
                "ocrEnabled={}",
                static_cast<int>(pipelineMode), llmEnabled, ocrEnabled);

    if (!llmEnabled) {
        SPDLOG_INFO("LLM post-processing skipped: ASR-only mode");
        co_return finalText;
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

    co_return result;
}

QImage VoiceTextProcessor::captureFocusedContextImage() const
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
    saveOcrDebugImage(image);
    return image;
}

} // namespace talkinput
