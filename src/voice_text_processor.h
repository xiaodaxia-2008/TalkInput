#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <functional>

namespace talkinput
{

class LlmPostProcessor;
class OcrRecognizer;

class VoiceTextProcessor final : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const QString &)>;

    explicit VoiceTextProcessor(QObject *parent = nullptr);

    void processFinalText(const QString &text, QObject *receiver,
                          Callback callback);

private:
    QImage captureFocusedContextImage() const;

    LlmPostProcessor *m_llmPostProcessor = nullptr;
    OcrRecognizer *m_ocrRecognizer = nullptr;
};

} // namespace talkinput
