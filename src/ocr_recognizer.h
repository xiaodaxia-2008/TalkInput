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
    virtual QRect focusedTextInputRect() const;
    virtual WId focusedTextInputWindowId() const;
    virtual QString focusedTextInputScreenName() const;
    virtual QImage captureFocusedTextInputImage() const;
    virtual QCoro::Task<QString> recognizeText(const QImage &image) = 0;

    static std::expected<std::unique_ptr<OcrRecognizer>, QString>
    createFromPreset(const OcrPreset &preset,
                     QObject *parent = nullptr);
};

} // namespace talkinput
