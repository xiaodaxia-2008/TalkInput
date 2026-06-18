#pragma once

#include "ocr_service.h"

namespace talkinput
{

class WindowsOcrService final : public OcrService
{
    Q_OBJECT

public:
    explicit WindowsOcrService(QObject *parent = nullptr);

    bool isAvailable() const override;
    QRect focusedTextInputRect() const override;
    WId focusedTextInputWindowId() const override;
    QString focusedTextInputScreenName() const override;
    QImage captureFocusedTextInputImage() const override;
    void recognizeText(const QImage &image, QObject *receiver,
                       Callback callback) override;
};

} // namespace talkinput
