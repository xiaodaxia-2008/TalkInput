#include "voice_text_processor.h"
#include "asr_config.h"
#include "llm_post_processor.h"
#include "logging.h"
#include "ocr_config.h"
#include "ocr_recognizer.h"
#include "utils.h"

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

void VoiceTextProcessor::processFinalText(const QString &text,
                                          PipelineMode pipelineMode,
                                          QObject *receiver, Callback callback)
{
    const QString finalText = text.trimmed();
    if (finalText.isEmpty()) {
        return;
    }

    const bool llmEnabled = pipelineMode != PipelineMode::AsrOnly;
    const bool ocrEnabled = pipelineMode == PipelineMode::AsrLlmOcr;

    SPDLOG_INFO("VoiceTextProcessor received final text: mode={} llmEnabled={} "
                "ocrEnabled={}",
                static_cast<int>(pipelineMode), llmEnabled, ocrEnabled);

    if (!llmEnabled) {
        SPDLOG_INFO("LLM post-processing skipped: ASR-only mode");
        if (callback) {
            callback(finalText);
        }
        return;
    }

    const bool ocrServiceAvailable =
        m_ocrRecognizer && m_ocrRecognizer->isAvailable();

    if (!ocrEnabled || !ocrServiceAvailable) {
        SPDLOG_INFO("OCR context skipped: ASR+LLM mode");
        m_llmPostProcessor->postProcess(
            finalText, {}, currentHotwordsText(), receiver,
            [callback = std::move(callback)](const QString &processedText) mutable {
                if (callback) {
                    callback(processedText.trimmed());
                }
            });
        return;
    }

    const QImage image = captureFocusedContextImage();
    if (image.isNull()) {
        SPDLOG_INFO("OCR context skipped: no focused screenshot");
        m_llmPostProcessor->postProcess(
            finalText, {}, currentHotwordsText(), receiver,
            [callback = std::move(callback)](const QString &processedText) mutable {
                if (callback) {
                    callback(processedText.trimmed());
                }
            });
        return;
    }

    SPDLOG_INFO("OCR context screenshot captured: {}x{}", image.width(),
                image.height());
    STATUSBAR_INFO("{}", tr("Reading focused input context..."));
    m_ocrRecognizer->recognizeText(
        image, receiver,
        [this, finalText, receiver,
         callback = std::move(callback)](const QString &contextText) mutable {
            const QString result = contextText.trimmed();
            SPDLOG_INFO("OCR context result received: {}", result);
            STATUSBAR_INFO("{}", tr("Post-processing recognition result..."));
            m_llmPostProcessor->postProcess(
                finalText, result, currentHotwordsText(), receiver,
                std::move(callback));
        });
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
