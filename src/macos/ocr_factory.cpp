#include "ocr_recognizer.h"

#include <QMetaObject>

namespace talkinput
{

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
    return new NullOcrRecognizer(parent);
}

} // namespace talkinput
