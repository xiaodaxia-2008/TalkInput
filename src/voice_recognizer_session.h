#pragma once

#include "json_utils.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <expected>
#include <memory>

namespace talkinput
{

class SpeechRecognizer;

class VoiceRecognizerSession final : public QObject
{
    Q_OBJECT

public:
    explicit VoiceRecognizerSession(QObject *parent = nullptr);
    ~VoiceRecognizerSession() override;

    bool isSpeechRecognitionModelLoaded() const;
    bool isRecognitionStreamRunning() const;
    bool acceptsExternalAudio() const;
    SpeechRecognizer *speechRecognizer() const;

    std::expected<void, QString>
    loadSpeechRecognitionModel(const nlohmann::json &preset);
    void unloadSpeechRecognitionModel();
    void resetRecognitionStream();
    void feedRecognitionAudio(const QByteArray &pcm16, int sampleRate,
                              int channels);
    bool finishRunningRecognitionStream();

signals:
    void resultChanged(const QString &text, bool isFinal);

private:
    std::unique_ptr<SpeechRecognizer> m_recognizer;
};

} // namespace talkinput
