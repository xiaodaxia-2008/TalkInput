#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QString>

#include <expected>
#include <memory>

class QAudioSource;
class QIODevice;

namespace talkinput
{

class AudioInputCapture final : public QObject
{
    Q_OBJECT

public:
    explicit AudioInputCapture(QObject *parent = nullptr);
    ~AudioInputCapture() override;

    std::expected<void, QString> start();
    void stop();
    bool isRunning() const;

signals:
    void pcm16Ready(const QByteArray &pcm16, int sampleRate, int channels);

private:
    std::unique_ptr<QAudioSource> m_audioSource;
    QIODevice *m_audioDevice = nullptr;
    QAudioFormat m_audioFormat;
};

} // namespace talkinput
