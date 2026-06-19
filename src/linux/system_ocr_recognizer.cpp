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

QRect SystemOcrRecognizer::focusedTextInputRect() const
{
    return {};
}

WId SystemOcrRecognizer::focusedTextInputWindowId() const
{
    return 0;
}

QString SystemOcrRecognizer::focusedTextInputScreenName() const
{
    return {};
}

QImage SystemOcrRecognizer::captureFocusedTextInputImage() const
{
    return {};
}

void SystemOcrRecognizer::recognizeText(const QImage &image, QObject *receiver,
                                     Callback callback)
{
    if (!receiver || !callback) {
        return;
    }
    QMetaObject::invokeMethod(
        receiver, [callback = std::move(callback)]() mutable { callback({}); },
        Qt::QueuedConnection);
}

} // namespace talkinput
