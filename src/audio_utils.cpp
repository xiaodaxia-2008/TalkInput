#include "audio_utils.h"
#include "logging.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <numeric>
#include <span>

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

bool savePcm16ToWav(const QByteArray &pcm16, int sampleRate, int channels,
                    const QString &filePath)
{
    if (pcm16.isEmpty()) {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        SPDLOG_WARN("Failed to write WAV: {}", filePath);
        return false;
    }

    const int dataSize = pcm16.size();
    const int fileSize = 36 + dataSize;
    const quint16 audioFormat = 1;
    const quint16 bitsPerSample = 16;
    const int byteRate = sampleRate * channels * bitsPerSample / 8;
    const quint16 blockAlign =
        static_cast<quint16>(channels * bitsPerSample / 8);

    auto write16 = [&](quint16 v) {
        file.write(reinterpret_cast<const char *>(&v), 2);
    };
    auto write32 = [&](quint32 v) {
        file.write(reinterpret_cast<const char *>(&v), 4);
    };

    file.write("RIFF", 4);
    write32(static_cast<quint32>(fileSize));
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    write32(16);
    write16(audioFormat);
    write16(static_cast<quint16>(channels));
    write32(static_cast<quint32>(sampleRate));
    write32(static_cast<quint32>(byteRate));
    write16(blockAlign);
    write16(bitsPerSample);
    file.write("data", 4);
    write32(static_cast<quint32>(dataSize));
    file.write(pcm16);

    SPDLOG_INFO("WAV saved: {}", filePath);
    return true;
}

// ── Silence-based audio segmentation ──────────────────────────────

namespace
{

struct SilenceRun
{
    int midpoint = 0;
    int sampleCount = 0;
};

float computeFrameRms(std::span<const float> data)
{
    return std::sqrt(
        std::inner_product(data.begin(), data.end(), data.begin(), 0.0f) /
        data.size());
}

std::vector<SilenceRun> findSilenceRuns(std::span<const float> samples,
                                        int sampleRate, int frameMs,
                                        int minSilenceMs, float silenceThresh)
{
    if (sampleRate <= 0 || frameMs <= 0 || minSilenceMs <= 0) {
        return {};
    }

    const int frameSize = sampleRate * frameMs / 1000;
    const int maxPos = samples.size() > static_cast<size_t>(INT_MAX)
                           ? INT_MAX
                           : static_cast<int>(samples.size());
    if (frameSize <= 0 || frameSize > maxPos) {
        return {};
    }

    std::vector<SilenceRun> runs;
    int runFrames = 0;
    int runStart = 0;
    const int requiredFrames = std::max(
        1, minSilenceMs / frameMs + (minSilenceMs % frameMs != 0 ? 1 : 0));

    const auto appendRun = [&]() {
        if (runFrames >= requiredFrames) {
            const int sampleCount = runFrames * frameSize;
            runs.push_back({runStart + sampleCount / 2, sampleCount});
        }
    };

    for (int i = 0; i + frameSize <= maxPos; i += frameSize) {
        if (computeFrameRms(samples.subspan(i, frameSize)) < silenceThresh) {
            if (runFrames == 0) {
                runStart = i;
            }
            ++runFrames;
        }
        else {
            appendRun();
            runFrames = 0;
        }
    }
    appendRun();

    return runs;
}

} // namespace

std::vector<int> findSilenceSplits(std::span<const float> samples,
                                   int sampleRate, int frameMs,
                                   int minSilenceMs, float silenceThresh)
{
    std::vector<int> splits;
    for (const auto &run : findSilenceRuns(samples, sampleRate, frameMs,
                                           minSilenceMs, silenceThresh))
    {
        splits.push_back(run.midpoint);
    }
    return splits;
}

int findBestSilenceSplit(std::span<const float> samples, int sampleRate,
                         int minSample, int maxSample, int frameMs,
                         int minSilenceMs, float silenceThresh)
{
    const int sampleCount = samples.size() > static_cast<size_t>(INT_MAX)
                                ? INT_MAX
                                : static_cast<int>(samples.size());
    minSample = std::clamp(minSample, 0, sampleCount);
    maxSample = std::clamp(maxSample, minSample, sampleCount);

    SilenceRun best;
    for (const auto &run :
         findSilenceRuns(samples.first(maxSample), sampleRate, frameMs,
                         minSilenceMs, silenceThresh))
    {
        if (run.midpoint < minSample) {
            continue;
        }
        if (run.sampleCount > best.sampleCount ||
            (run.sampleCount == best.sampleCount &&
             run.midpoint > best.midpoint))
        {
            best = run;
        }
    }
    return best.midpoint;
}

std::vector<AudioSegment>
segmentAudioBySilence(std::span<const float> samples, int sampleRate,
                      int maxChunkSeconds, int targetChunkSeconds, int frameMs,
                      int minSilenceMs, float silenceThresh)
{
    std::vector<AudioSegment> blocks;
    if (samples.empty() || sampleRate <= 0 || maxChunkSeconds <= 0 ||
        targetChunkSeconds <= 0)
    {
        return blocks;
    }

    const qint64 maxSamples64 =
        static_cast<qint64>(maxChunkSeconds) * sampleRate;
    const int totalSamples = static_cast<int>(samples.size());
    if (maxSamples64 >= totalSamples) {
        return {{0, totalSamples}};
    }

    const int maxSamples = static_cast<int>(maxSamples64);
    const qint64 targetSamples64 =
        static_cast<qint64>(targetChunkSeconds) * sampleRate;
    const int targetSamples =
        static_cast<int>(std::min(maxSamples64, targetSamples64));

    int start = 0;
    while (totalSamples - start > maxSamples) {
        const auto remaining = samples.subspan(static_cast<size_t>(start));
        int split = findBestSilenceSplit(remaining, sampleRate, targetSamples,
                                         maxSamples, frameMs, minSilenceMs,
                                         silenceThresh);
        if (split == 0) {
            split = maxSamples;
        }
        blocks.push_back({start, split});
        start += split;
    }
    blocks.push_back({start, totalSamples - start});

    return blocks;
}

} // namespace talkinput
