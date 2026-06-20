#pragma once

#include "voice_input_controller.h"

#include <QCoro/QCoroTask>
#include <QImage>
#include <QObject>
#include <QString>

#include <memory>

namespace talkinput
{

class LlmPostProcessor;
class OcrRecognizer;

class VoiceTextProcessor final : public QObject
{
    Q_OBJECT

public:
    explicit VoiceTextProcessor(QObject *parent = nullptr);
    ~VoiceTextProcessor() override;

    QCoro::Task<QString> processFinalText(const QString &text,
                                          PipelineMode pipelineMode);

private:
    QImage captureFocusedContextImage() const;

    std::unique_ptr<LlmPostProcessor> m_llmPostProcessor;
    std::unique_ptr<OcrRecognizer> m_ocrRecognizer;
};

} // namespace talkinput
