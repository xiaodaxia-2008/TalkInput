#pragma once

#include "ocr_recognizer.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace talkinput
{

class PpOcrV6OcrRecognizer final : public OcrRecognizer
{
    Q_OBJECT

public:
    explicit PpOcrV6OcrRecognizer(QObject *parent = nullptr);
    ~PpOcrV6OcrRecognizer() override;

    bool isAvailable() const override;
    QCoro::Task<QString> recognizeText(const QImage &image) override;
    QCoro::Task<OcrResult> recognizeDetailed(const QImage &image) override;

private:
    bool ensureInitialized();
    OcrResult recognizeWithPpOcr(const QImage &image);
    QString modelDir() const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::mutex m_initMutex;
    std::atomic<bool> m_initialized{false};
};

} // namespace talkinput
