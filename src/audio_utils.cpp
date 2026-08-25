#include "audio_utils.h"
#include "logging.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QBuffer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QProcess>
#include <QTimer>
#include <QtEndian>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <numeric>
#include <span>

namespace zenny
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
decodeAudioDeviceToPcm16(QIODevice *device, int timeoutMs)
{
    if (!device || !device->isOpen() || !device->isReadable()) {
        return std::unexpected(
            QStringLiteral("Audio input device is not open for reading."));
    }

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

    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        error = QStringLiteral("Audio decoding timed out.");
        loop.quit();
    });

    decoder.setSourceDevice(device);
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

std::expected<DecodedAudioFile, QString>
decodeAudioDataToPcm16(const QByteArray &data, int timeoutMs)
{
    if (data.isEmpty()) {
        return std::unexpected(QStringLiteral("Audio data is empty."));
    }

    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly)) {
        return std::unexpected(QStringLiteral("Failed to open audio data."));
    }

    return decodeAudioDeviceToPcm16(&buffer, timeoutMs);
}

std::expected<DecodedAudioFile, QString>
decodeAudioFileToPcm16(const QString &path, int timeoutMs)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(QStringLiteral("Failed to open audio file: %1")
                                   .arg(file.errorString()));
    }

    return decodeAudioDeviceToPcm16(&file, timeoutMs);
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

bool savePcm16ToM4a(const QByteArray &pcm16, int sampleRate, int channels,
                    const QString &filePath)
{
    if (pcm16.isEmpty() || sampleRate <= 0 || channels <= 0) {
        return false;
    }

    QProcess ffmpeg;
    ffmpeg.setProgram(QStringLiteral("ffmpeg"));
    ffmpeg.setArguments(
        {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-f"),
         QStringLiteral("s16le"), QStringLiteral("-ar"),
         QString::number(sampleRate), QStringLiteral("-ac"),
         QString::number(channels), QStringLiteral("-i"),
         QStringLiteral("pipe:0"), QStringLiteral("-codec:a"),
         QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("128k"),
         QStringLiteral("-movflags"), QStringLiteral("+faststart"),
         QStringLiteral("-y"), filePath});
    ffmpeg.setProcessChannelMode(QProcess::SeparateChannels);
    ffmpeg.start();
    if (!ffmpeg.waitForStarted()) {
        SPDLOG_WARN("Failed to start ffmpeg for M4A: {}", ffmpeg.errorString());
        return false;
    }

    qint64 offset = 0;
    while (offset < pcm16.size()) {
        const qint64 written =
            ffmpeg.write(pcm16.constData() + offset, pcm16.size() - offset);
        if (written <= 0) {
            SPDLOG_WARN("Failed to write PCM to ffmpeg for M4A: {}",
                        ffmpeg.errorString());
            ffmpeg.kill();
            ffmpeg.waitForFinished();
            QFile::remove(filePath);
            return false;
        }
        offset += written;
        if (!ffmpeg.waitForBytesWritten(-1) && offset < pcm16.size()) {
            SPDLOG_WARN("Failed to flush PCM to ffmpeg for M4A: {}",
                        ffmpeg.errorString());
            ffmpeg.kill();
            ffmpeg.waitForFinished();
            QFile::remove(filePath);
            return false;
        }
    }
    ffmpeg.closeWriteChannel();

    if (!ffmpeg.waitForFinished(-1) ||
        ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0)
    {
        const QString error =
            QString::fromUtf8(ffmpeg.readAllStandardError()).trimmed();
        SPDLOG_WARN("Failed to save M4A {}: {}", filePath,
                    error.isEmpty() ? ffmpeg.errorString() : error);
        QFile::remove(filePath);
        return false;
    }

    if (!QFileInfo::exists(filePath) || QFileInfo(filePath).size() <= 0) {
        SPDLOG_WARN("ffmpeg produced no M4A file: {}", filePath);
        return false;
    }

    SPDLOG_INFO("M4A saved: {}", filePath);
    return true;
}

// ── Silence-based audio segmentation ──────────────────────────────

namespace
{

constexpr int minSegmentSeconds = 5;

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

int findLatestSilenceSplit(std::span<const float> samples, int sampleRate,
                           int minSample, int maxSample, int frameMs,
                           int minSilenceMs, float silenceThresh)
{
    const int sampleCount = samples.size() > static_cast<size_t>(INT_MAX)
                                ? INT_MAX
                                : static_cast<int>(samples.size());
    minSample = std::clamp(minSample, 0, sampleCount);
    maxSample = std::clamp(maxSample, minSample, sampleCount);
    int latest = 0;
    for (const auto &run :
         findSilenceRuns(samples.first(maxSample), sampleRate, frameMs,
                         minSilenceMs, silenceThresh))
    {
        if (run.midpoint < minSample) {
            continue;
        }
        latest = run.midpoint;
    }

    return latest;
}

std::vector<AudioSegment> segmentAudioBySilence(std::span<const float> samples,
                                                int sampleRate,
                                                int maxChunkSeconds,
                                                int frameMs, int minSilenceMs,
                                                float silenceThresh)
{
    std::vector<AudioSegment> blocks;
    if (samples.empty() || sampleRate <= 0 || maxChunkSeconds <= 0) {
        return blocks;
    }

    const qint64 maxSamples64 =
        static_cast<qint64>(maxChunkSeconds) * sampleRate;
    const int totalSamples = static_cast<int>(samples.size());
    if (maxSamples64 >= totalSamples) {
        return {{0, totalSamples}};
    }

    const int maxSamples = static_cast<int>(maxSamples64);
    int start = 0;
    while (totalSamples - start > maxSamples) {
        const auto remaining = samples.subspan(static_cast<size_t>(start));
        const int minSplitSamples =
            std::min(maxSamples, minSegmentSeconds * sampleRate);
        int split = findBestSilenceSplit(remaining, sampleRate, minSplitSamples,
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

} // namespace zenny
