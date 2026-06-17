#pragma once

#include "speech_recognizer.h"

#include <QObject>

class QThread;

namespace talkinput
{

class AsrService final : public QObject
{
    Q_OBJECT

public:
    explicit AsrService(QObject *parent = nullptr);
    ~AsrService() override;

    void setModelDirectory(const QString &dir);

    QString modelDirectory() const
    {
        return m_modelDir;
    }

    bool isModelLoaded() const
    {
        return m_modelLoaded;
    }

    SpeechRecognizer *recognizer() const
    {
        return m_recognizer;
    }

    bool isStreamingModel() const
    {
        return m_streamingMode;
    }

public slots:
    void loadModel();
    void unloadModel();
    void startSession();
    void feedAudio(const QByteArray &pcm16, int sampleRate, int channels);
    void finishSession();
    void abortSession();

signals:
    void modelLoadResult(bool success, const QString &error);
    void resultChanged(const QString &text, bool isFinal);

private:
    SpeechRecognizer::Config detectAndConfigure(const QString &modelDir);

    SpeechRecognizer *m_recognizer = nullptr;
    QString m_modelDir;
    bool m_modelLoaded = false;
    bool m_streamingMode = false;
};

} // namespace talkinput
