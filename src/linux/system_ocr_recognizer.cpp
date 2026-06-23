#include "../system_ocr_recognizer.h"

#include "../logging.h"

namespace talkinput
{

SystemOcrRecognizer::SystemOcrRecognizer(QObject *parent) : OcrRecognizer(parent)
{
}

bool SystemOcrRecognizer::isAvailable() const
{
    return false;
}

QImage SystemOcrRecognizer::captureContextImage() const
{
return {};
}

QCoro::Task<QString> SystemOcrRecognizer::recognizeText(const QImage &image)
{
    co_return {};
}

} // namespace talkinput
