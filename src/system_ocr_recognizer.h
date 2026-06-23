#pragma once

#include "ocr_recognizer.h"

namespace talkinput
{

class SystemOcrRecognizer final : public OcrRecognizer
{
    Q_OBJECT

public:
    explicit SystemOcrRecognizer(QObject *parent = nullptr);

    bool isAvailable() const override;
    QImage captureContextImage() const override;
    QCoro::Task<QString> recognizeText(const QImage &image) override;
};

} // namespace talkinput
