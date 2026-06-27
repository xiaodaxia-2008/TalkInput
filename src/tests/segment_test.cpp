#include "audio_utils.h"
#include "logging.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <cstdio>
#include <vector>

namespace
{

std::vector<float> pcm16ToFloats(const QByteArray &pcm16)
{
    const int count = pcm16.size() / 2;
    std::vector<float> floats(static_cast<size_t>(count));
    const auto *data = reinterpret_cast<const qint16 *>(pcm16.constData());
    for (int i = 0; i < count; ++i) {
        floats[static_cast<size_t>(i)] = static_cast<float>(data[i]) / 32768.0f;
    }
    return floats;
}

QByteArray floatsToPcm16(std::span<const float> samples)
{
    QByteArray pcm16;
    pcm16.reserve(static_cast<int>(samples.size()) * 2);
    for (float s : samples) {
        const float clamped = std::clamp(s, -1.0f, 1.0f);
        const qint16 sample = static_cast<qint16>(clamped * 32767.0f);
        pcm16.append(reinterpret_cast<const char *>(&sample), 2);
    }
    return pcm16;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString audioPath =
        (argc > 1) ? QString::fromLocal8Bit(argv[1])
                   : QStringLiteral(
                         "C:/Users/xiaoz/AppData/Roaming/ZenShawn/TalkInput/"
                         "asr_audios/asr-20260627-233933-140.wav");

    SPDLOG_INFO("Loading: {}", audioPath);

    auto decoded = talkinput::decodeAudioFileToPcm16(audioPath);
    if (!decoded) {
        SPDLOG_ERROR("Failed to decode audio: {}", decoded.error());
        return 1;
    }

    const auto &pcm16 = decoded->pcm16;
    const int sampleRate = decoded->sampleRate;
    const int channels = decoded->channels;
    SPDLOG_INFO("Decoded: {} samples, {}Hz, {}ch", pcm16.size() / 2, sampleRate,
                channels);

    // Convert to mono float
    std::vector<float> floats;
    if (channels == 1) {
        floats = pcm16ToFloats(pcm16);
    }
    else {
        // Mix down to mono
        const int frameCount = pcm16.size() / (2 * channels);
        floats.resize(static_cast<size_t>(frameCount));
        const auto *data = reinterpret_cast<const qint16 *>(pcm16.constData());
        for (int i = 0; i < frameCount; ++i) {
            int64_t sum = 0;
            for (int ch = 0; ch < channels; ++ch) {
                sum += data[static_cast<size_t>(i * channels + ch)];
            }
            floats[static_cast<size_t>(i)] =
                static_cast<float>(sum) / (32768.0f * channels);
        }
    }

    // ── Segment ──
    auto segs = talkinput::segmentAudioBySilence(floats, sampleRate);
    SPDLOG_INFO("Segments: {}", segs.size());
    for (size_t i = 0; i < segs.size(); ++i) {
        const auto &seg = segs[i];
        const double startSec =
            static_cast<double>(seg.startSample) / sampleRate;
        const double durSec = static_cast<double>(seg.sampleCount) / sampleRate;
        SPDLOG_INFO("  [{}] start={:.3f}s  dur={:.3f}s  samples={}", i,
                    startSec, durSec, seg.sampleCount);
    }

    // ── Save each segment ──
    const QString outDir =
        QDir(QCoreApplication::applicationDirPath()).filePath("asr_segments");
    QDir().mkpath(outDir);
    const QString ts =
        QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz");

    for (size_t i = 0; i < segs.size(); ++i) {
        const auto &seg = segs[i];
        const auto span =
            std::span(floats).subspan(static_cast<size_t>(seg.startSample),
                                      static_cast<size_t>(seg.sampleCount));
        const QByteArray pcm16seg = floatsToPcm16(span);

        const QString path = QDir(outDir).filePath(
            QString("seg-%1-%2.wav")
                .arg(ts)
                .arg(static_cast<int>(i), 3, 10, QLatin1Char('0')));

        talkinput::savePcm16ToWav(pcm16seg, sampleRate, 1, path);
    }

    SPDLOG_INFO("Segments saved to: {}", outDir);

    return 0;
}
