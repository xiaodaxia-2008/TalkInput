#include "system_ocr_service.h"

#include "logging.h"

namespace talkinput
{

SystemOcrService::SystemOcrService(QObject *parent) : OcrRecognizer(parent)
{
}

bool SystemOcrService::isAvailable() const
{
    return false;
}

QRect SystemOcrService::focusedTextInputRect() const
{
    return {};
}

WId SystemOcrService::focusedTextInputWindowId() const
{
    return 0;
}

QString SystemOcrService::focusedTextInputScreenName() const
{
    return {};
}

QImage SystemOcrService::captureFocusedTextInputImage() const
{
    return {};
}

void SystemOcrService::recognizeText(const QImage &image, QObject *receiver,
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
