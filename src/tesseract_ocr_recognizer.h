#pragma once

#include "ocr_recognizer.h"

namespace talkinput
{

class TesseractOcrRecognizer final : public OcrRecognizer
{
    Q_OBJECT

public:
    explicit TesseractOcrRecognizer(QObject *parent = nullptr);
    ~TesseractOcrRecognizer() override;

    bool isAvailable() const override;
    QCoro::Task<QString> recognizeText(const QImage &image) override;

private:
    QString tessdataDir() const;
};

} // namespace talkinput
