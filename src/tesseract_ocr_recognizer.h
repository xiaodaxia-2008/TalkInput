#pragma once

#include "ocr_recognizer.h"

#include <atomic>
#include <mutex>

namespace tesseract
{
class TessBaseAPI;
}

namespace talkinput
{

class TesseractOcrRecognizer final : public OcrRecognizer
{
    Q_OBJECT

public:
    explicit TesseractOcrRecognizer(QObject *parent = nullptr);
    ~TesseractOcrRecognizer() override;

    bool isAvailable() const override;
    QImage captureFocusedTextInputImage() const override;
    QCoro::Task<QString> recognizeText(const QImage &image) override;

private:
    bool ensureInitialized();
    QString recognizeWithTesseract(const QImage &image);

    QString tessdataDir() const;

    std::unique_ptr<tesseract::TessBaseAPI> m_api;
    std::mutex m_initMutex;
    std::atomic<bool> m_initialized{false};
};

} // namespace talkinput
