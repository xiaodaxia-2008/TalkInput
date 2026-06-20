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

    bool isSpeechRecognitionModelLoaded() const;

    bool acceptsExternalAudio() const;

    SpeechRecognizer *speechRecognizer() const;

signals:
    void listeningChanged(bool listening);
    void finalTextCommitted(const QString &text);
    void speechRecognitionModelLoadResult(bool success, const QString &error);

public slots:
    bool startListening();
    void stopListening();
    void loadSpeechRecognitionModel(const nlohmann::json &preset);
    void unloadSpeechRecognitionModel();
    bool startSpeechRecognitionSession();
    void feedSpeechRecognitionAudio(const QByteArray &pcm16, int sampleRate,
                                    int channels);
    void finishSpeechRecognitionSession();

private:
    enum class FinalTextAction
    {
        RecordHistoryOnly,
        PasteAndRecordHistory
    };

    bool startRecording(FinalTextAction finalTextAction);
    void onResult(const QString &text, bool isFinal);
    void postProcessFinalText(const QString &text,
                              FinalTextAction finalTextAction);
    void commitFinalText(const QString &text, FinalTextAction finalTextAction);
    void enterListeningState(const char *logMessage);
    void leaveListeningState();
    void showOverlay();
    void hideOverlay();
    bool beginRecognitionFlow(FinalTextAction finalTextAction);
    void resetRecognitionFlow();

    std::unique_ptr<VoiceRecognizerSession> m_recognizerSession;
    std::unique_ptr<AudioInputCapture> m_audioCapture;
    std::unique_ptr<TextInjector> m_textInjector;
    std::unique_ptr<VoiceTextProcessor> m_textProcessor;
    std::unique_ptr<VoiceHotkey> m_hotkey;

    bool m_isListening = false;

    std::unique_ptr<VoiceOverlay> m_overlay;
    QString m_lastResult;
    bool m_busy = false;
    bool m_processingFinalText = false;
    FinalTextAction m_finalTextAction = FinalTextAction::RecordHistoryOnly;
};

} // namespace talkinput
