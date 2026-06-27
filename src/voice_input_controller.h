#pragma once

#include "app_config.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QCoro/QCoroTask>
#include <QElapsedTimer>
#include <QObject>

#include <expected>
#include <memory>

template <typename T>
class QPromise;

class QAudioSource;
class QIODevice;
class QThread;

namespace talkinput
{

class LlmPostProcessor;
class OcrRecognizer;
class SpeechRecognizer;
class VoiceHotkey;
class VoiceOverlay;

enum class PipelineMode
{
    AsrOnly,
    AsrLlm,
    AsrLlmOcr
};

enum class PipelineStage
{
    Idle,
    Recording,
    Recognizing,
    ReadingContext,
    Polishing
};

PipelineMode pipelineModeFromString(const std::string &s);
std::string pipelineModeToString(PipelineMode mode);
QString pipelineModeDisplayName(PipelineMode mode);

class VoiceInputController final : public QObject
{
    Q_OBJECT

public:
    static VoiceInputController *instance();

    explicit VoiceInputController(QObject *parent = nullptr);
    ~VoiceInputController() override;

    bool isListening() const
    {
        return m_stage == PipelineStage::Recording ||
               m_stage == PipelineStage::Recognizing;
    }

    PipelineStage stage() const
    {
        return m_stage;
    }

    bool isSpeechRecognitionModelLoaded() const;

    SpeechRecognizer *speechRecognizer() const;

    std::string loadedPresetId() const;

    void reregisterTriggerHotkey();
    void reregisterModeSwitchHotkey();
    void cyclePipelineMode();

signals:
    void listeningChanged(bool listening);
    void finalTextCommitted(const QString &text);
    void modeChanged(PipelineMode mode);

public slots:
    bool startListening();
    void stopListening();

    void loadSpeechRecognitionModel(const AsrPreset &preset);
    void unloadSpeechRecognitionModel();

    void reloadOcrRecognizer();

    bool startSpeechRecognitionSession();
    void feedSpeechRecognitionAudio(const QByteArray &pcm16, int sampleRate,
                                    int channels);
    void finishSpeechRecognitionSession();

private:
    QCoro::Task<void> executePipeline();
    void setStage(PipelineStage stage);
    void onResult(const QString &text, bool isFinal);
    std::expected<void, QString> startAudioCapture();
    void stopAudioCapture();
    bool isAudioCaptureRunning() const;
    void queueRecognizerReset();
    void queueRecognizerAudio(const QByteArray &pcm16, int sampleRate,
                              int channels);
    void queueRecognizerFinish();

    SpeechRecognizer *m_recognizer = nullptr;
    std::unique_ptr<QThread> m_recognizerThread;
    std::unique_ptr<LlmPostProcessor> m_llmPostProcessor;
    std::unique_ptr<OcrRecognizer> m_ocrRecognizer;
    std::unique_ptr<VoiceHotkey> m_hotkey;

    std::unique_ptr<VoiceOverlay> m_overlay;
    std::unique_ptr<QAudioSource> m_audioSource;
    QIODevice *m_audioDevice = nullptr;
    QAudioFormat m_audioFormat;
    QByteArray m_capturedAudio;
    QString m_lastResult;
    QElapsedTimer m_stopRequestedAt;
    PipelineStage m_stage = PipelineStage::Idle;
    PipelineMode m_pipelineMode = PipelineMode::AsrLlmOcr;
    QPromise<QString> *m_finalResultPromise = nullptr;
    std::string m_loadedPresetId;
};

} // namespace talkinput
