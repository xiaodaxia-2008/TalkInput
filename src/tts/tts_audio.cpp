#include "tts_audio.h"

namespace talkinput
{

QByteArray resampleFloatToInt16(const float *samples, qsizetype n,
                                int inputRate, int outputRate)
{
    if (!samples || n <= 0 || inputRate <= 0 || outputRate <= 0) {
        return {};
    }
    if (inputRate == outputRate) {
        QByteArray out(static_cast<qsizetype>(n) * 2, Qt::Uninitialized);
        qint16 *dst = reinterpret_cast<qint16 *>(out.data());
        for (qsizetype i = 0; i < n; ++i) {
            const float clamped = qBound(-1.0f, samples[i], 1.0f);
            dst[i] = static_cast<qint16>(
                clamped < 0 ? clamped * 0x8000 : clamped * 0x7fff);
        }
        return out;
    }

    const double ratio = static_cast<double>(outputRate) / inputRate;
    const qsizetype outN =
        qMax<qsizetype>(1, qRound64(n * ratio));
    QByteArray out(outN * 2, Qt::Uninitialized);
    qint16 *dst = reinterpret_cast<qint16 *>(out.data());
    for (qsizetype i = 0; i < outN; ++i) {
        const double srcPos = i / ratio;
        qsizetype i0 = static_cast<qsizetype>(srcPos);
        i0 = qBound<qsizetype>(0, i0, n - 1);
        qsizetype i1 = qMin(i0 + 1, n - 1);
        const double frac = srcPos - i0;
        const double sample =
            samples[i0] * (1.0 - frac) + samples[i1] * frac;
        const float clamped = qBound(-1.0f, static_cast<float>(sample), 1.0f);
        dst[i] = static_cast<qint16>(
            clamped < 0 ? clamped * 0x8000 : clamped * 0x7fff);
    }
    return out;
}

QByteArray pcm16ToWav(const QByteArray &pcm16, int sampleRate)
{
    const quint32 dataSize = static_cast<quint32>(pcm16.size());
    const quint32 byteRate = static_cast<quint32>(sampleRate) * 2;
    QByteArray wav;
    wav.reserve(44 + pcm16.size());
    wav.append("RIFF", 4);
    const quint32 riffSize = 36 + dataSize;
    wav.append(reinterpret_cast<const char *>(&riffSize), 4);
    wav.append("WAVE", 4);
    wav.append("fmt ", 4);
    const quint32 fmtChunkSize = 16;
    const quint16 audioFormat = 1; // PCM
    const quint16 channels = 1;
    const quint32 samplesPerSec = static_cast<quint32>(sampleRate);
    const quint16 blockAlign = 2;
    const quint16 bitsPerSample = 16;
    wav.append(reinterpret_cast<const char *>(&fmtChunkSize), 4);
    wav.append(reinterpret_cast<const char *>(&audioFormat), 2);
    wav.append(reinterpret_cast<const char *>(&channels), 2);
    wav.append(reinterpret_cast<const char *>(&samplesPerSec), 4);
    wav.append(reinterpret_cast<const char *>(&byteRate), 4);
    wav.append(reinterpret_cast<const char *>(&blockAlign), 2);
    wav.append(reinterpret_cast<const char *>(&bitsPerSample), 2);
    wav.append("data", 4);
    wav.append(reinterpret_cast<const char *>(&dataSize), 4);
    wav.append(pcm16);
    return wav;
}

} // namespace talkinput
