#include "ocr_recognizer.h"
#include "system_ocr_recognizer.h"

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
    return {};
}

std::unique_ptr<OcrRecognizer>
OcrRecognizer::createFromConfig(const nlohmann::json &preset,
                                QString *errorMessage, QObject *parent)
{
    const QString typeName = jsonString(preset, "type");

    if (typeName == QStringLiteral("System")) {
        return std::make_unique<SystemOcrRecognizer>(parent);
    }

    if (errorMessage) {
        *errorMessage =
            QStringLiteral("Unsupported OCR type: %1").arg(typeName);
    }
    return nullptr;
}

} // namespace talkinput
