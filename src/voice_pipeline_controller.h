#pragma once

#include "app_config.h"
#include "ocr_recognizer.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QCoro/QCoroTask>
#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>

#include <expected>
#include <functional>
#include <memory>

template <typename T>
class QPromise;

class QAudioSource;
class QIODevice;
class QThread;

namespace talkinput
{

class LlmPostProcessor;
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
    Polishing,
    ApiTranscribing
};

/// Result of a transcription submitted through the shared recognizer by the
/// HTTP API server. `error` is non-empty on failure.
struct ApiTranscriptionResult
{
    QString text;
    QString error;
    double duration = 0.0;
};

/// Result of an OCR request submitted through the shared OCR recognizer.
/// `error` is non-empty on failure.
struct ApiOcrResult
{
    QString text;
    QString error;
    QVector<OcrTextBlock> blocks;
};

PipelineMode pipelineModeFromString(const std::string &s);
std::string pipelineModeToString(PipelineMode mode);
QString pipelineModeDisplayName(PipelineMode mode);

class VoicePipelineController final : public QObject
{
    Q_OBJECT

public:
    static VoicePipelineController *instance();

    explicit VoicePipelineController(QObject *parent = nullptr);
    ~VoicePipelineController() override;

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

    /// Transcribes @p pcm16 on the shared recognizer thread. Runs on the GUI
    /// thread; rejects the request (error set) when the recognizer is busy
    /// with a microphone session or another API request. The callback is
    /// invoked on the GUI thread once the result is available.
    void submitApiTranscription(
        const QByteArray &pcm16, int sampleRate, int channels,
        std::function<void(const ApiTranscriptionResult &)> callback);

    /// Recognizes @p image with the configured OCR provider. Runs on the GUI
    /// thread and rejects the request when another input pipeline is active.
    void submitApiOcr(const QImage &image,
                      std::function<void(const ApiOcrResult &)> callback);

    /// Recognizes an image and returns text boxes for visual OCR previews.
    void submitDetailedOcr(const QImage &image,
                           std::function<void(const ApiOcrResult &)> callback);

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

private:
    QCoro::Task<void> executePipeline();
    QCoro::Task<void>
    executeApiOcr(QImage image, QPointer<OcrRecognizer> recognizer,
                  std::function<void(const ApiOcrResult &)> callback);
    QCoro::Task<void>
    executeDetailedOcr(QImage image, QPointer<OcrRecognizer> recognizer,
                       std::function<void(const ApiOcrResult &)> callback);
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
