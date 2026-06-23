#include "tesseract_ocr_recognizer.h"
#include "logging.h"

#include <QCoro/QCoroFuture>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPromise>
#include <QThreadPool>

#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

#include <spdlog/stopwatch.h>

namespace talkinput
{

TesseractOcrRecognizer::TesseractOcrRecognizer(QObject *parent)
    : OcrRecognizer(parent)
    , m_api(std::make_unique<tesseract::TessBaseAPI>())
{
}

TesseractOcrRecognizer::~TesseractOcrRecognizer()
{
    if (m_initialized.load()) {
        m_api->End();
    }
}

bool TesseractOcrRecognizer::isAvailable() const
{
    return QFileInfo::exists(
        tessdataDir() + QStringLiteral("/chi_sim.traineddata"));
}

bool TesseractOcrRecognizer::ensureInitialized()
{
    if (m_initialized.load(std::memory_order_acquire)) {
        return true;
    }

    std::lock_guard lock(m_initMutex);
    if (m_initialized.load(std::memory_order_relaxed)) {
        return true;
    }

    const QString dataPath =
        tessdataDir() + QStringLiteral("/chi_sim.traineddata");
    const QString dataDir = QFileInfo(dataPath).absolutePath();

    if (m_api->Init(dataDir.toUtf8().toStdString().c_str(), "chi_sim") != 0) {
        SPDLOG_ERROR("Tesseract OCR: failed to initialize with data dir: {}",
                     dataDir);
        return false;
    }

    m_api->SetPageSegMode(tesseract::PSM_AUTO);
    m_initialized.store(true, std::memory_order_release);
    SPDLOG_DEBUG("Tesseract OCR: engine initialized");
    return true;
}

QString TesseractOcrRecognizer::recognizeWithTesseract(const QImage &image)
{
    if (image.isNull()) {
        SPDLOG_DEBUG("Tesseract OCR: skipped empty image");
        return {};
    }

    if (!ensureInitialized()) {
        return {};
    }

    // Scale down large captures by 2x to balance speed and accuracy
    constexpr int scaleThreshold = 1200;
    QImage workImage = image;
    if (workImage.width() > scaleThreshold || workImage.height() > scaleThreshold) {
        workImage = workImage.scaled(workImage.size() / 2, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
        SPDLOG_DEBUG("Tesseract OCR: scaled from {}x{} to {}x{}",
                     image.width(), image.height(),
                     workImage.width(), workImage.height());
    }

    // Convert QImage to Pix (leptonica)
    const QImage rgbImage = workImage.convertToFormat(QImage::Format_RGB32);
    const int bytesPerPixel = 4;
    const int width = rgbImage.width();
    const int height = rgbImage.height();

    Pix *pix = pixCreate(width, height, 32);
    if (!pix) {
        SPDLOG_ERROR("Tesseract OCR: failed to create Pix");
        return {};
    }

    // Copy pixel data (QImage RGB32 is BGRA on little-endian)
    for (int y = 0; y < height; ++y) {
        const auto *src = rgbImage.scanLine(y);
        l_uint32 *dst = pixGetData(pix) + y * pixGetWpl(pix);
        for (int x = 0; x < width; ++x) {
            // QImage Format_RGB32 stores B,G,R,A on little-endian (x86)
            dst[x] = (static_cast<l_uint32>(src[x * bytesPerPixel + 2]) << 24) |   // R
                     (static_cast<l_uint32>(src[x * bytesPerPixel + 1]) << 16) |   // G
                     (static_cast<l_uint32>(src[x * bytesPerPixel + 0]) << 8)  |   // B
                     0xFF;                                                         // A
        }
    }

    m_api->SetImage(pix);

    // Get recognized text
    spdlog::stopwatch sw;
    const char *utf8Text = m_api->GetUTF8Text();
    SPDLOG_DEBUG("Tesseract OCR: took {:.3f} sec", sw);

    QString result;
    if (utf8Text) {
        result = QString::fromUtf8(utf8Text).trimmed();
        delete[] utf8Text;
    }

    m_api->Clear();
    pixDestroy(&pix);

    if (result.isEmpty()) {
        SPDLOG_WARN("Tesseract OCR: empty result");
    }

    return result;
}

QCoro::Task<QString> TesseractOcrRecognizer::recognizeText(const QImage &image)
{
    if (image.isNull()) {
        co_return QString();
    }

    QPromise<QString> promise;
    promise.start();
    auto future = promise.future();

    const QImage imageCopy = image.copy();
    QThreadPool::globalInstance()->start(
        [this, imageCopy, promise = std::move(promise)]() mutable {
            QString text;
            try {
                text = recognizeWithTesseract(imageCopy);
            }
            catch (const std::exception &e) {
                SPDLOG_WARN("Tesseract OCR: failed: {}", e.what());
            }
            promise.addResult(text);
            promise.finish();
        });

    co_return co_await future;
}

QString TesseractOcrRecognizer::tessdataDir() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/tessdata");
}

} // namespace talkinput
