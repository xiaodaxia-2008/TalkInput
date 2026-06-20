#pragma once

#include "json_utils.h"

#include <QByteArray>
#include <QObject>

#include <memory>

namespace talkinput
{

class AudioInputCapture;
class SpeechRecognizer;
class TextInjector;
class VoiceHotkey;
class VoiceRecognizerSession;
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

    bool isModelLoaded() const;

    bool acceptsExternalAudio() const;

    SpeechRecognizer *recognizer() const;

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
    void enterListeningState(const char *logMessage);
    void leaveListeningState();
    void showOverlay();
    void hideOverlay();

    VoiceRecognizerSession *m_recognizerSession = nullptr;
    AudioInputCapture *m_audioCapture = nullptr;
    TextInjector *m_textInjector = nullptr;
    VoiceTextProcessor *m_textProcessor = nullptr;
    VoiceHotkey *m_hotkey = nullptr;

    bool m_isListening = false;

    std::unique_ptr<VoiceOverlay> m_overlay;
    QString m_lastResult;
    bool m_pendingResult = false;
};

} // namespace talkinput
