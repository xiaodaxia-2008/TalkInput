#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class QThread;

namespace talkinput
{

struct TranscriptionResult
{
    QString text;
    double duration = 0.0;
    QString language;
};

/// Transcribes raw audio bytes (any format QAudioDecoder can open).
/// Returns the recognized text, or fills @p errorMessage and returns an
/// empty result on failure.
using ApiTranscriber = std::function<TranscriptionResult(
    const QByteArray &audioData, const QString &fileName,
    QString *errorMessage)>;

/// Exposes the local AI services through an OpenAI-compatible HTTP API:
///
///   POST /v1/audio/transcriptions  (multipart/form-data: file, model)
///   POST /v1/ocr                    (multipart/form-data: file)
///   POST /v1/images/ocr             (alias for /v1/ocr)
///   POST /v1/audio/speech          (JSON: input, voice, speed,
///                                   response_format; returns audio)
///   GET  /v1/models
///   GET  /health
///
/// The server runs on its own worker thread. Transcription and OCR requests
/// are delegated to the shared recognizers owned by VoicePipelineController,
/// while TTS requests use the configured backend. No second ASR/OCR model copy
/// is loaded. Requests are rejected while a microphone session is running.
/// TTS backends ("edge" = Microsoft Edge online, "melo" = offline MeloTTS)
/// return 24 kHz 16-bit mono PCM.
class LocalAiApiServer final : public QObject
{
    Q_OBJECT

public:
    explicit LocalAiApiServer(QObject *parent = nullptr);
    ~LocalAiApiServer() override;

    static LocalAiApiServer *instance();

    /// Overrides the transcription implementation (used by tests).
    void setTranscriber(ApiTranscriber transcriber);

public slots:
    /// (Re)applies host/port/enabled/api-key from appConfig().settings.
    void applySettings();

    /// Stops listening and tears down the worker thread.
    void shutdown();

signals:
    void listeningChanged(bool listening);
    void serverStarted(quint16 port);
    void errorOccurred(const QString &message);
    void transcriptionCompleted(const QString &text);

private:
    class Core;

    std::unique_ptr<QThread> m_thread;
    Core *m_core = nullptr;
    ApiTranscriber m_transcriber;
};

} // namespace talkinput
