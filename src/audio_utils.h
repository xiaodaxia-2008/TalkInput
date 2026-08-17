#pragma once

#include <QAudioFormat>
#include <QByteArray>
#include <QString>

#include <expected>
#include <span>
#include <vector>

namespace talkinput
{

void appendPcm16Sample(QByteArray &audioData, qint16 sample);
qint16 floatSampleToPcm16(float sample);
QByteArray convertAudioToPcm16(const QByteArray &audioData,
                               const QAudioFormat &format);

struct DecodedAudioFile
{
    QByteArray pcm16;
    int sampleRate = 0;
    int channels = 0;
};

std::expected<DecodedAudioFile, QString>
decodeAudioFileToPcm16(const QString &path, int timeoutMs = 30000);

bool savePcm16ToWav(const QByteArray &pcm16, int sampleRate, int channels,
                    const QString &filePath);

// ── Silence-based audio segmentation ──────────────────────────────

struct AudioSegment
{
    int startSample;
    int sampleCount;
};

/// Find split points where audio RMS stays below @p silenceThresh
/// for at least @p minSilenceMs.
std::vector<int> findSilenceSplits(std::span<const float> samples,
                                   int sampleRate, int frameMs = 30,
                                   int minSilenceMs = 300,
                                   float silenceThresh = 0.02f);

/// Find the midpoint of the longest silence between @p minSample and
/// @p maxSample. Returns 0 when no suitable silence exists.
int findBestSilenceSplit(std::span<const float> samples, int sampleRate,
                         int minSample, int maxSample, int frameMs = 30,
                         int minSilenceMs = 300, float silenceThresh = 0.02f);

/// Find the latest suitable silence between @p minSample and @p maxSample.
/// Returns 0 when no suitable silence exists.
int findLatestSilenceSplit(std::span<const float> samples, int sampleRate,
                           int minSample, int maxSample, int frameMs = 30,
                           int minSilenceMs = 300, float silenceThresh = 0.02f);

/// Pack natural speech segments greedily without exceeding
/// @p maxChunkSeconds. If no silence is available before the limit, split at
/// the hard limit.
std::vector<AudioSegment>
segmentAudioBySilence(std::span<const float> samples, int sampleRate,
                      int maxChunkSeconds = 15, int frameMs = 30,
                      int minSilenceMs = 300, float silenceThresh = 0.02f);

} // namespace talkinput
