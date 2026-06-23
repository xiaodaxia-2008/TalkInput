#include "ocr_recognizer.h"
#include "platform_utils.h"
#include "system_ocr_recognizer.h"
#include "tesseract_ocr_recognizer.h"

#include <QGuiApplication>
#include <QScreen>

namespace talkinput
{

OcrRecognizer::OcrRecognizer(QObject *parent) : QObject(parent)
{
}

OcrRecognizer::~OcrRecognizer() = default;

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
    if (preset.type == "System") {
        return std::make_unique<SystemOcrRecognizer>(parent);
    }
    if (preset.type == "Tesseract") {
        return std::make_unique<TesseractOcrRecognizer>(parent);
    }

    return std::unexpected(QStringLiteral("Unsupported OCR type: %1")
                               .arg(QString::fromStdString(preset.type)));
}

} // namespace talkinput
