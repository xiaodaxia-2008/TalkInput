#pragma once

#include "speech_recognizer.h"

#include <QByteArray>
#include <QObject>

#include <memory>

namespace talkinput
{

class AudioInputCapture;
class VoiceHotkey;
class VoiceTextProcessor;
class VoiceOverlay;

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
    void onResult(const QString &text, bool isFinal);
    void postProcessFinalText(const QString &text);
    void injectFinalText(const QString &text);
    void sendText(const QString &text);
    void showOverlay();
    void hideOverlay();

    std::unique_ptr<SpeechRecognizer> m_recognizer;
    AudioInputCapture *m_audioCapture = nullptr;
    VoiceTextProcessor *m_textProcessor = nullptr;
    VoiceHotkey *m_hotkey = nullptr;

    bool m_isListening = false;

    std::unique_ptr<VoiceOverlay> m_overlay;
    QString m_lastResult;
    bool m_pendingResult = false;
};

} // namespace talkinput
