#pragma once

#include "app_config.h"
#include "json_utils.h"

#include <QCoro/QCoroTask>
#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>
#include <expected>
#include <memory>
#include <qwindowdefs.h>

namespace talkinput
{

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

    static std::expected<std::unique_ptr<OcrRecognizer>, QString>
    createFromPreset(const OcrPreset &preset,
                     QObject *parent = nullptr);
};

} // namespace talkinput
