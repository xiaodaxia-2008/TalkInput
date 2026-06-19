#include "audio_input_capture.h"
#include "audio_utils.h"
#include "logging.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>

namespace talkinput
{

AudioInputCapture::AudioInputCapture(QObject *parent) : QObject(parent)
{
}

AudioInputCapture::~AudioInputCapture()
{
    stop();
}

std::expected<void, QString> AudioInputCapture::start()
{
    if (m_audioSource) {
        return {};
    }

    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        SPDLOG_ERROR("No audio input device");
        return std::unexpected(tr("No microphone available"));
    }

    m_audioFormat = inputDevice.preferredFormat();
    if (!m_audioFormat.isValid() ||
        m_audioFormat.sampleFormat() == QAudioFormat::Unknown)
    {
        m_audioFormat = QAudioFormat();
        m_audioFormat.setSampleRate(48000);
        m_audioFormat.setChannelCount(1);
        m_audioFormat.setSampleFormat(QAudioFormat::Int16);
    }

    if (!inputDevice.isFormatSupported(m_audioFormat)) {
        SPDLOG_ERROR("Audio format not supported");
        return std::unexpected(tr("Microphone format not supported."));
    }

    m_audioSource = std::make_unique<QAudioSource>(inputDevice, m_audioFormat);
    m_audioDevice = m_audioSource->start();
    if (!m_audioDevice) {
        SPDLOG_ERROR("Failed to start microphone");
        m_audioSource.reset();
        return std::unexpected(tr("Failed to start microphone"));
    }

    connect(m_audioDevice, &QIODevice::readyRead, this, [this]() {
        if (!m_audioDevice) {
            return;
        }

        const QByteArray audioData = m_audioDevice->readAll();
        const QByteArray pcm16 = convertAudioToPcm16(audioData, m_audioFormat);
        if (!pcm16.isEmpty()) {
            emit pcm16Ready(pcm16, m_audioFormat.sampleRate(),
                            m_audioFormat.channelCount());
        }
    });

    return {};
}

void AudioInputCapture::stop()
{
    if (m_audioSource) {
        m_audioSource->stop();
    }
    m_audioDevice = nullptr;
    m_audioSource.reset();
}

bool AudioInputCapture::isRunning() const
{
    return m_audioSource != nullptr;
}

} // namespace talkinput
