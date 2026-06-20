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

    bool isModelLoaded() const;
    bool acceptsExternalAudio() const;
    SpeechRecognizer *recognizer() const;

    std::expected<void, QString> loadModel(const nlohmann::json &preset);
    void unloadModel();
    void resetStream();
    void feedAudio(const QByteArray &pcm16, int sampleRate, int channels);
    bool finishRunningStream();

signals:
    void resultChanged(const QString &text, bool isFinal);

private:
    std::unique_ptr<SpeechRecognizer> m_recognizer;
};

} // namespace talkinput
