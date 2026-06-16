#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

namespace talkinput
{

class SpeechRecognizer final : public QObject
{
    Q_OBJECT

public:
    struct Config
    {
        QString modelDir;
        QString encoderFile = "encoder.int8.onnx";
        QString decoderFile = "decoder.onnx";
        QString joinerFile = "joiner.int8.onnx";
        QString tokensFile = "tokens.txt";
        QString modelingUnit = "cjkchar";
        QString hotwordsText;
        float hotwordsScore = 1.5F;
        int sampleRate = 16000;
        int featureDim = 80;
        int numThreads = 2;
    };

    explicit SpeechRecognizer(QObject *parent = nullptr);
    ~SpeechRecognizer() override;

    bool start(const Config &config, QString *errorMessage);
    void stop();
    bool isRunning() const;

    void acceptPcm16(const QByteArray &audioData, int sampleRate,
                     int channelCount);
    void finish();

signals:
    void logMessage(const QString &message);
    void resultChanged(const QString &text, bool isFinal);

private:
    class Impl;
    Impl *m_impl = nullptr;
};

} // namespace talkinput
