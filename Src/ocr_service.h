#pragma once

#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>
#include <functional>
#include <qwindowdefs.h>

namespace talkinput
{

class OcrService : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const QString &)>;

    explicit OcrService(QObject *parent = nullptr);
    ~OcrService() override;

    virtual bool isAvailable() const = 0;
    virtual QRect focusedTextInputRect() const;
    virtual WId focusedTextInputWindowId() const;
    virtual QString focusedTextInputScreenName() const;
    virtual QImage captureFocusedTextInputImage() const;
    virtual void recognizeText(const QImage &image, QObject *receiver,
                               Callback callback) = 0;
};

OcrService *createOcrService(QObject *parent = nullptr);

} // namespace talkinput
