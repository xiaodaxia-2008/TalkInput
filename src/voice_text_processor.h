#pragma once

#include "voice_input_controller.h"

#include <QImage>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

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
    ~VoiceTextProcessor() override;

    void processFinalText(const QString &text, PipelineMode pipelineMode,
                          QObject *receiver, Callback callback);

private:
    QImage captureFocusedContextImage() const;

    std::unique_ptr<LlmPostProcessor> m_llmPostProcessor;
    std::unique_ptr<OcrRecognizer> m_ocrRecognizer;
};

} // namespace talkinput
