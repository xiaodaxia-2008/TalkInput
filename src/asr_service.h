#pragma once

#include "recognizers/speech_recognizer.h"

#include <QObject>
#include <memory>

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
    void setPunctuationModelDir(const QString &dir);

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
        return m_recognizer.get();
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
    QString findPunctuationModelPath(const QString &modelDir) const;

    std::unique_ptr<SpeechRecognizer> m_recognizer;
    QString m_modelDir;
    QString m_punctuationModelDir;
    bool m_modelLoaded = false;
    bool m_streamingMode = false;
};

} // namespace talkinput
