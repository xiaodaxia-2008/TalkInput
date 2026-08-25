#pragma once

#include "app_config.h"
#include "json_utils.h"

#include <QCoro/QCoroTask>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QVector>
#include <expected>
#include <memory>
#include <qwindowdefs.h>

namespace zenny
{

struct OcrTextBlock
{
    QString text;
    QRectF bounds;
};

struct OcrResult
{
    QString text;
    QVector<OcrTextBlock> blocks;
};

class OcrRecognizer : public QObject
{
    Q_OBJECT

public:
    explicit OcrRecognizer(QObject *parent = nullptr);
    ~OcrRecognizer() override;

    virtual bool isAvailable() const = 0;

    /// Captures an image of the context under the cursor (the window at the
    /// cursor position) using full-screen grab + crop for reliability.
    virtual QImage captureContextImage() const;

    virtual QCoro::Task<QString> recognizeText(const QImage &image) = 0;

    /// Returns recognized text together with optional image-space text boxes.
    /// Providers without box support fall back to text-only recognition.
    virtual QCoro::Task<OcrResult> recognizeDetailed(const QImage &image);

    static std::expected<std::unique_ptr<OcrRecognizer>, QString>
    createFromPreset(const OcrPreset &preset, QObject *parent = nullptr);
};

} // namespace zenny
