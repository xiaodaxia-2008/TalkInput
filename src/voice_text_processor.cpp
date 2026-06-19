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
    m_llmPostProcessor = new LlmPostProcessor(this);
    auto ocr = OcrRecognizer::createFromConfig(currentOcrPreset(), this);
    if (!ocr) {
        SPDLOG_WARN("OcrRecognizer: failed to create: {}", ocr.error());
    }
    else {
        m_ocrRecognizer = ocr->release();
    }
}

void VoiceTextProcessor::processFinalText(const QString &text,
                                          QObject *receiver, Callback callback)
{
    const QString finalText = text.trimmed();
    if (finalText.isEmpty()) {
        return;
    }

    auto submitToLlm = [this, finalText, receiver,
                        callback = std::move(callback)](
                           const QString &ocrContext) mutable {
        if (m_llmPostProcessor->isEnabled()) {
            SPDLOG_DEBUG("OCR context sent to LLM: {}", ocrContext.trimmed());
            spdlog::get("statusbar")
                ->info("{}", tr("Post-processing recognition result..."));
        }
        m_llmPostProcessor->postProcess(
            finalText, ocrContext, currentHotwordsText(), receiver,
            [finalText, callback = std::move(callback)](
                const QString &processedText) mutable {
                SPDLOG_DEBUG(
                    "Voice input final text after LLM: input='{}' output='{}'",
                    finalText, processedText);
                if (callback) {
                    callback(processedText.trimmed());
                }
            });
    };

    const bool llmEnabled = m_llmPostProcessor->isEnabled();
    const bool ocrEnabled = ocrContextEnabledForAsr();
    const bool ocrServiceAvailable =
        m_ocrRecognizer && m_ocrRecognizer->isAvailable();
    SPDLOG_DEBUG("OCR context flow: llmEnabled={} ocrEnabled={} "
                 "ocrServiceAvailable={}",
                 llmEnabled, ocrEnabled, ocrServiceAvailable);

    if (!llmEnabled) {
        SPDLOG_DEBUG("OCR context skipped: LLM post-processing is disabled");
        submitToLlm({});
        return;
    }
    if (!ocrEnabled) {
        SPDLOG_DEBUG("OCR context skipped: OCR focused context is disabled");
        submitToLlm({});
        return;
    }
    if (!ocrServiceAvailable) {
        SPDLOG_DEBUG("OCR context skipped: OCR service is unavailable");
        submitToLlm({});
        return;
    }

    const QImage image = captureFocusedContextImage();
    if (image.isNull()) {
        SPDLOG_DEBUG("OCR context skipped: no focused screenshot");
        submitToLlm({});
        return;
    }

    SPDLOG_DEBUG("OCR context screenshot captured: {}x{}", image.width(),
                 image.height());
    spdlog::get("statusbar")
        ->info("{}", tr("Reading focused input context..."));
    m_ocrRecognizer->recognizeText(
        image, receiver,
        [submitToLlm =
             std::move(submitToLlm)](const QString &contextText) mutable {
            const QString result = contextText.trimmed();
            SPDLOG_DEBUG("OCR context result received: {}", result);
            submitToLlm(result);
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
