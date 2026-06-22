#pragma once

#include "app_config.h"
#include "json_utils.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QString>

#include <expected>
#include <memory>
#include <string>
#include <vector>

class QAudioSource;
class QIODevice;

struct SherpaOnnxOfflinePunctuation;

namespace talkinput
{

class SpeechRecognizer : public QObject
{
    Q_OBJECT

public:
    enum class Type
    {
        StreamingParaformer,
        SenseVoice,
        FunASRNano,
    };

    explicit SpeechRecognizer(QObject *parent = nullptr);
    ~SpeechRecognizer() override;

    virtual std::expected<void, QString>
    start(const nlohmann::json &config) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual bool isStreaming() const = 0;

    virtual void acceptPcm16(const QByteArray &audioData, int sampleRate,
                             int channelCount) = 0;
    virtual void finish() = 0;
    virtual void resetStream() = 0;

    std::expected<void, QString> startCapture();
    void stopCapture();
    bool isCaptureRunning() const;

    static std::expected<std::unique_ptr<SpeechRecognizer>, QString>
    createFromPreset(const AsrPreset &preset,
                     QObject *parent = nullptr);

signals:
    void resultChanged(const QString &text, bool isFinal);

protected:
    std::expected<void, QString>
    prepareRecognizer(const nlohmann::json &config);
    void stopPunctuation();
    QString addPunctuation(const QString &text) const;

    static QString modelPath(const QString &modelDir, const QString &fileName);
    static std::expected<QString, QString>
    configuredModelPath(const nlohmann::json &config, const char *configField);
    static std::expected<void, QString> fileExists(const QString &path);
    static std::expected<void, QString> pathExists(const QString &path);
    static QString decodeSherpaText(const char *text);
    static int appendPcm16AsMonoFloat(const QByteArray &audioData,
                                      int channelCount,
                                      std::vector<float> *samples);

private:
    const SherpaOnnxOfflinePunctuation *m_punct = nullptr;
    std::unique_ptr<QAudioSource> m_audioSource;
    QIODevice *m_audioDevice = nullptr;
    QAudioFormat m_audioFormat;
};
} // namespace talkinput
