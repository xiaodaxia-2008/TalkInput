#pragma once

#include "ocr_recognizer.h"

namespace talkinput
{

class SystemOcrService final : public OcrRecognizer
{
    Q_OBJECT

public:
    explicit SystemOcrService(QObject *parent = nullptr);

    bool isAvailable() const override;
    QRect focusedTextInputRect() const override;
    WId focusedTextInputWindowId() const override;
    QString focusedTextInputScreenName() const override;
    QImage captureFocusedTextInputImage() const override;
    void recognizeText(const QImage &image, QObject *receiver,
                       Callback callback) override;
};

} // namespace talkinput
