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

/// Choose the silence split nearest to @p targetSample inside
/// [@p minSample, @p maxSample]. Falls back to the longest silence in range.
/// Returns 0 when no suitable silence exists.
int findNearestSilenceSplit(std::span<const float> samples, int sampleRate,
                            int targetSample, int minSample, int maxSample,
                            int frameMs = 30, int minSilenceMs = 300,
                            float silenceThresh = 0.02f);

/// Keep audio intact up to @p maxChunkSeconds. Longer audio is split at the
/// longest available silence after @p targetChunkSeconds, or at the hard
/// limit when no suitable silence exists.
std::vector<AudioSegment>
segmentAudioBySilence(std::span<const float> samples, int sampleRate,
                      int maxChunkSeconds = 15, int targetChunkSeconds = 10,
                      int frameMs = 30, int minSilenceMs = 300,
                      float silenceThresh = 0.02f);

} // namespace talkinput
