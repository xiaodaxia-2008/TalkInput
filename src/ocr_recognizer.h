#pragma once

#include "json_utils.h"

#include <QImage>
#include <QObject>
#include <QRect>
#include <QString>
#include <functional>
#include <memory>
#include <qwindowdefs.h>

namespace talkinput
{

class OcrRecognizer : public QObject
{
    Q_OBJECT

public:
    enum class Type
    {
        System,
    };

    using Callback = std::function<void(const QString &)>;

    explicit OcrRecognizer(QObject *parent = nullptr);
    ~OcrRecognizer() override;

    virtual bool isAvailable() const = 0;
    virtual QRect focusedTextInputRect() const;
    virtual WId focusedTextInputWindowId() const;
    virtual QString focusedTextInputScreenName() const;
    virtual QImage captureFocusedTextInputImage() const;
    virtual void recognizeText(const QImage &image, QObject *receiver,
                               Callback callback) = 0;

    static std::unique_ptr<OcrRecognizer>
    createFromConfig(const nlohmann::json &preset, QString *errorMessage,
                     QObject *parent = nullptr);
};

} // namespace talkinput
