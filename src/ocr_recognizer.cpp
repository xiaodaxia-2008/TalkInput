#include "ocr_recognizer.h"
#include "platform_utils.h"
#ifdef ZENNY_HAS_PPOCRV6
#include "ppocrv6_ocr_recognizer.h"
#endif

#include <QGuiApplication>
#include <QScreen>

namespace zenny
{

OcrRecognizer::OcrRecognizer(QObject *parent) : QObject(parent)
{
}

OcrRecognizer::~OcrRecognizer() = default;

QCoro::Task<OcrResult> OcrRecognizer::recognizeDetailed(const QImage &image)
{
    OcrResult result;
    result.text = co_await recognizeText(image);
    co_return result;
}

QImage OcrRecognizer::captureContextImage() const
{
    // 1. Get the native window info under the cursor
    const auto winInfo = nativeWindowInfoAtCursor();
    if (!winInfo.wid || winInfo.geometry.isEmpty()) {
        return {};
    }

    const QRect &windowRect = winInfo.geometry;

    // 3. Find which physical screen the window is on
    QScreen *screen = QGuiApplication::screenAt(windowRect.center());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return {};
    }

    // 4. Capture the full screen of that display
    const QImage fullScreen = screen->grabWindow(0).toImage();
    if (fullScreen.isNull()) {
        return {};
    }

    // 5. Convert absolute coords to screen-relative and crop
    const QPoint screenTopLeft = screen->geometry().topLeft();
    const QRect relativeRect(windowRect.x() - screenTopLeft.x(),
                             windowRect.y() - screenTopLeft.y(),
                             windowRect.width(), windowRect.height());

    return fullScreen.copy(relativeRect);
}

std::expected<std::unique_ptr<OcrRecognizer>, QString>
OcrRecognizer::createFromPreset(const OcrPreset &preset, QObject *parent)
{
#ifdef ZENNY_HAS_PPOCRV6
    if (preset.type == "PpOcrV6") {
        return std::make_unique<PpOcrV6OcrRecognizer>(parent);
    }
#endif

    return std::unexpected(QStringLiteral("Unsupported OCR type: %1")
                               .arg(QString::fromStdString(preset.type)));
}

} // namespace zenny
