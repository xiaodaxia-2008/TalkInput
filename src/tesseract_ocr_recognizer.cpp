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

namespace talkinput
{

namespace
{

QString recognizeWithTesseract(const QImage &image, const QString &dataPath)
{
    if (image.isNull()) {
        SPDLOG_DEBUG("Tesseract OCR: skipped empty image");
        return {};
    }

    const QString dataDir = QFileInfo(dataPath).absolutePath();

    tesseract::TessBaseAPI api;
    if (api.Init(dataDir.toUtf8().toStdString().c_str(), "chi_sim") != 0) {
        SPDLOG_ERROR("Tesseract OCR: failed to initialize with data dir: {}",
                     dataDir);
        return {};
    }

    api.SetPageSegMode(tesseract::PSM_AUTO);

    // Convert QImage to Pix (leptonica)
    const QImage rgbImage = image.convertToFormat(QImage::Format_RGB32);
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

    api.SetImage(pix);

    // Get recognized text
    const char *utf8Text = api.GetUTF8Text();
    QString result;
    if (utf8Text) {
        result = QString::fromUtf8(utf8Text).trimmed();
        delete[] utf8Text;
    }

    api.End();
    pixDestroy(&pix);

    if (!result.isEmpty()) {
        SPDLOG_DEBUG("Tesseract OCR: result length={}", result.size());
    }
    else {
        SPDLOG_WARN("Tesseract OCR: empty result");
    }

    return result;
}

} // namespace

TesseractOcrRecognizer::TesseractOcrRecognizer(QObject *parent)
    : OcrRecognizer(parent)
{
}

TesseractOcrRecognizer::~TesseractOcrRecognizer() = default;

bool TesseractOcrRecognizer::isAvailable() const
{
    return QFileInfo::exists(tessdataDir() + QStringLiteral("/chi_sim.traineddata"));
}

QCoro::Task<QString> TesseractOcrRecognizer::recognizeText(const QImage &image)
{
    if (image.isNull()) {
        co_return QString();
    }

    const QString dataPath = tessdataDir() + QStringLiteral("/chi_sim.traineddata");

    QPromise<QString> promise;
    promise.start();
    auto future = promise.future();

    const QImage imageCopy = image.copy();
    QThreadPool::globalInstance()->start(
        [imageCopy, dataPath, promise = std::move(promise)]() mutable {
            QString text;
            try {
                text = recognizeWithTesseract(imageCopy, dataPath);
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
