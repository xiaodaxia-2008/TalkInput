#include "ocr_recognizer.h"

#include <QMetaObject>

#ifdef Q_OS_WIN
#include "system_ocr_service.h"
#endif

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

namespace
{

class NullOcrRecognizer final : public OcrRecognizer
{
public:
    using OcrRecognizer::OcrRecognizer;

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

OcrRecognizer *createOcrRecognizer(QObject *parent)
{
#ifdef Q_OS_WIN
    return new SystemOcrService(parent);
#else
    return new NullOcrRecognizer(parent);
#endif
}

} // namespace talkinput
