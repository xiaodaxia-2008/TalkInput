#pragma once

#include "ocr_recognizer.h"

namespace talkinput
{

class RapidOcrRecognizer final : public OcrRecognizer
{
    Q_OBJECT

public:
    using OcrRecognizer::OcrRecognizer;

    bool isAvailable() const override;
    void recognizeText(const QImage &image, QObject *receiver,
                       Callback callback) override;
};

} // namespace talkinput
