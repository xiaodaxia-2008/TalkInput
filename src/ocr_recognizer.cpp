#include "ocr_recognizer.h"
#include "system_ocr_recognizer.h"

#include <QCursor>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>

namespace talkinput
{

OcrRecognizer::OcrRecognizer(QObject *parent) : QObject(parent)
{
}

OcrRecognizer::~OcrRecognizer() = default;

QRect OcrRecognizer::focusedTextInputRect() const
{
    return {};
}

WId OcrRecognizer::focusedTextInputWindowId() const
{
    return 0;
}

QString OcrRecognizer::focusedTextInputScreenName() const
{
    return {};
}

QImage OcrRecognizer::captureFocusedTextInputImage() const
{
    const QString screenName = focusedTextInputScreenName();
    QScreen *screen = nullptr;
    if (!screenName.isEmpty()) {
        for (QScreen *s : QGuiApplication::screens()) {
            if (s && s->name().compare(screenName, Qt::CaseInsensitive) == 0) {
                screen = s;
                break;
            }
        }
    }
    if (!screen) {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return {};
    }

    return screen->grabWindow(0).toImage();
}

std::expected<std::unique_ptr<OcrRecognizer>, QString>
OcrRecognizer::createFromPreset(const OcrPreset &preset, QObject *parent)
{
    if (preset.type == "System") {
        return std::make_unique<SystemOcrRecognizer>(parent);
    }

    return std::unexpected(
        QStringLiteral("Unsupported OCR type: %1")
            .arg(QString::fromStdString(preset.type)));
}

} // namespace talkinput
