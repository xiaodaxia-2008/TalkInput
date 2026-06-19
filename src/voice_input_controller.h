#pragma once

#include "speech_recognizer.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QImage>
#include <QObject>

#include <memory>

class QAudioSource;
class QHotkey;
class QIODevice;
class QWidget;

namespace talkinput
{

class LlmPostProcessor;
class OcrRecognizer;

class VoiceInputController final : public QObject
{
    Q_OBJECT

public:
    static VoiceInputController *instance();

    explicit VoiceInputController(QObject *parent = nullptr);
    ~VoiceInputController() override;

    bool isListening() const
    {
        return m_isListening;
    }

    bool isModelLoaded() const
    {
        return m_recognizer != nullptr;
    }

    bool acceptsExternalAudio() const;

    SpeechRecognizer *recognizer() const
    {
        return m_recognizer.get();
    }

signals:
    void listeningChanged(bool listening);
    void finalTextCommitted(const QString &text);
    void modelLoadResult(bool success, const QString &error);

public slots:
    bool startListening();
    void stopListening();
    void loadModel(const nlohmann::json &preset);
    void unloadModel();
    void startSession();
    void feedAudio(const QByteArray &pcm16, int sampleRate, int channels);
    void finishSession();

private:
    void registerHotKey();
    void unregisterHotKey();
    void onResult(const QString &text, bool isFinal);
    void postProcessFinalText(const QString &text);
    QImage captureFocusedContextImage() const;
    void injectFinalText(const QString &text);
    void sendText(const QString &text);
    void showOverlay();
    void hideOverlay();

    std::unique_ptr<SpeechRecognizer> m_recognizer;
    LlmPostProcessor *m_llmPostProcessor = nullptr;
    OcrRecognizer *m_ocrRecognizer = nullptr;

    std::unique_ptr<QAudioSource> m_audioSource;
    QIODevice *m_audioDevice = nullptr;
    QAudioFormat m_audioFormat;
    QHotkey *m_hotkey = nullptr;
    bool m_isListening = false;

    std::unique_ptr<QWidget> m_overlay;
    QString m_lastResult;
    bool m_pendingResult = false;
};

} // namespace talkinput
