#include "audio_utils.h"

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

} // namespace talkinput
