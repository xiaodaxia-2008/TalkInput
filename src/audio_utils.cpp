#include "audio_utils.h"
#include "logging.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace talkinput
{

void appendPcm16Sample(QByteArray &audioData, qint16 sample)
{
    const qsizetype offset = audioData.size();
    audioData.resize(offset + static_cast<qsizetype>(sizeof(qint16)));
    qToLittleEndian<qint16>(
        sample, reinterpret_cast<uchar *>(audioData.data() + offset));
}

qint16 floatSampleToPcm16(float sample)
{
    const float clamped = std::clamp(sample, -1.0F, 1.0F);
    return static_cast<qint16>(clamped * 32767.0F);
}

QByteArray convertAudioToPcm16(const QByteArray &audioData,
                               const QAudioFormat &format)
{
    if (audioData.isEmpty()) {
        return {};
    }

    if (format.sampleFormat() == QAudioFormat::Int16) {
        return audioData;
    }

    QByteArray pcm16;

    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
        pcm16.reserve(audioData.size() * 2);
        for (const char byte : audioData) {
            const auto sample = static_cast<unsigned char>(byte);
            appendPcm16Sample(
                pcm16,
                static_cast<qint16>((static_cast<int>(sample) - 128) << 8));
        }
        break;
    case QAudioFormat::Int32: {
        const int sampleCount =
            audioData.size() / static_cast<int>(sizeof(qint32));
        pcm16.reserve(sampleCount * 2);
        const auto *data =
            reinterpret_cast<const uchar *>(audioData.constData());
        for (int i = 0; i < sampleCount; ++i) {
            const qint32 sample =
                qFromLittleEndian<qint32>(data + i * sizeof(qint32));
            appendPcm16Sample(pcm16, static_cast<qint16>(sample >> 16));
        }
        break;
    }
    case QAudioFormat::Float: {
        const int sampleCount =
            audioData.size() / static_cast<int>(sizeof(float));
        pcm16.reserve(sampleCount * 2);
        for (int i = 0; i < sampleCount; ++i) {
            float sample = 0.0F;
            std::memcpy(&sample,
                        audioData.constData() +
                            i * static_cast<int>(sizeof(float)),
                        sizeof(float));
            appendPcm16Sample(pcm16, floatSampleToPcm16(sample));
        }
        break;
    }
    default:
        break;
    }

    return pcm16;
}

std::expected<DecodedAudioFile, QString>
decodeAudioFileToPcm16(const QString &path, int timeoutMs)
{
    QAudioDecoder decoder;
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    DecodedAudioFile decoded;
    bool ok = false;
    QString error;

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &decoder, [&]() {
        const QAudioBuffer buf = decoder.read();
        const QAudioFormat format = buf.format();
        if (decoded.sampleRate == 0) {
            decoded.sampleRate = format.sampleRate();
            decoded.channels = format.channelCount();
        }
        else if (decoded.sampleRate != format.sampleRate() ||
                 decoded.channels != format.channelCount())
        {
            SPDLOG_WARN("Audio decoder format changed from {} channels {} "
                        "to {} channels {}",
                        decoded.sampleRate, decoded.channels,
                        format.sampleRate(), format.channelCount());
        }

        const QByteArray audioData(buf.constData<char>(), buf.byteCount());
        decoded.pcm16.append(convertAudioToPcm16(audioData, format));
    });

    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, [&]() {
        ok = true;
        loop.quit();
    });

    QObject::connect(&decoder,
                     static_cast<void (QAudioDecoder::*)(QAudioDecoder::Error)>(
                         &QAudioDecoder::error),
                     &loop, [&](QAudioDecoder::Error) {
                         error = decoder.errorString();
                         SPDLOG_ERROR("Audio decoder error: {}", error);
                         loop.quit();
                     });

    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    decoder.setSource(QUrl::fromLocalFile(path));
    decoder.start();

    timeoutTimer.start(timeoutMs);
    loop.exec();

    decoder.stop();

    if (!ok || decoded.pcm16.isEmpty()) {
        return std::unexpected(error);
    }

    if (decoded.sampleRate <= 0 || decoded.channels <= 0) {
        return std::unexpected(error);
    }

    return decoded;
}

} // namespace talkinput
