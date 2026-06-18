#include "ocr_service.h"

#include <QMetaObject>

#ifdef Q_OS_WIN
#include "Windows/ocr_service_windows.h"
#endif

namespace talkinput
{

OcrService::OcrService(QObject *parent) : QObject(parent)
{
}

OcrService::~OcrService() = default;

QRect OcrService::focusedTextInputRect() const
{
    return {};
}

WId OcrService::focusedTextInputWindowId() const
{
    return 0;
}

QString OcrService::focusedTextInputScreenName() const
{
    return {};
}

QImage OcrService::captureFocusedTextInputImage() const
{
    return {};
}

namespace
{

class NullOcrService final : public OcrService
{
public:
    using OcrService::OcrService;

    bool isAvailable() const override
    {
        return false;
    }

    void recognizeText(const QImage &, QObject *receiver,
                       Callback callback) override
    {
        if (!receiver || !callback) {
            return;
        }
        QMetaObject::invokeMethod(
            receiver,
            [callback = std::move(callback)]() mutable { callback({}); },
            Qt::QueuedConnection);
    }
};

} // namespace

OcrService *createOcrService(QObject *parent)
{
#ifdef Q_OS_WIN
    return new WindowsOcrService(parent);
#else
    return new NullOcrService(parent);
#endif
}

} // namespace talkinput
