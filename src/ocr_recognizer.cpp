#include "ocr_recognizer.h"

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

} // namespace talkinput
