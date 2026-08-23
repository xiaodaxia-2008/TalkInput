#include "app_config.h"
#include "audio_utils.h"
#include "logging.h"
#include "speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{

std::vector<float> pcm16ToFloats(const QByteArray &pcm16, int channels)
{
    if (channels <= 0 || pcm16.isEmpty()) return {};
    const int frameCount = pcm16.size() / (2 * channels);
    std::vector<float> floats(static_cast<size_t>(frameCount));
    const auto *data = reinterpret_cast<const qint16 *>(pcm16.constData());
    for (int i = 0; i < frameCount; ++i) {
        if (channels == 1) {
            floats[static_cast<size_t>(i)] = static_cast<float>(data[i]) / 32768.0f;
        } else {
            int64_t sum = 0;
            for (int ch = 0; ch < channels; ++ch) {
                sum += data[static_cast<size_t>(i * channels + ch)];
            }
            floats[static_cast<size_t>(i)] = static_cast<float>(sum) / (32768.0f * channels);
        }
    }
    return floats;
}

struct RawVadSegment {
    int start;
    int count;
};

// 1. Extract raw natural speech segments from Silero VAD
std::vector<RawVadSegment> extractVadSegments(const std::vector<float> &floats, int sampleRate)
{
    const QString vadPath = QDir::current().filePath("models/sherpa-onnx-silero-vad/silero_vad.onnx");
    if (!QFileInfo::exists(vadPath)) {
        SPDLOG_ERROR("Silero VAD not found: {}", vadPath.toStdString());
        return {};
    }

    SherpaOnnxVadModelConfig vadConfig;
    std::memset(&vadConfig, 0, sizeof(vadConfig));
    const std::string vadStr = vadPath.toUtf8().toStdString();
    vadConfig.silero_vad.model = vadStr.c_str();
    vadConfig.silero_vad.threshold = 0.5F;
    vadConfig.silero_vad.min_silence_duration = 0.3F; // 300ms silence
    vadConfig.silero_vad.min_speech_duration = 0.1F;
    vadConfig.silero_vad.window_size = 512;
    vadConfig.silero_vad.max_speech_duration = 20.0F;
    vadConfig.sample_rate = sampleRate;
    vadConfig.num_threads = 2;
    vadConfig.provider = "cpu";

    const SherpaOnnxVoiceActivityDetector *vad =
        SherpaOnnxCreateVoiceActivityDetector(&vadConfig, 30.0F);
    if (!vad) return {};

    std::vector<RawVadSegment> segments;
    constexpr int windowSamples = 512;
    const int totalSamples = static_cast<int>(floats.size());

    for (int offset = 0; offset < totalSamples; offset += windowSamples) {
        const int count = std::min(windowSamples, totalSamples - offset);
        SherpaOnnxVoiceActivityDetectorAcceptWaveform(vad, floats.data() + offset, count);
        while (!SherpaOnnxVoiceActivityDetectorEmpty(vad)) {
            const SherpaOnnxSpeechSegment *seg = SherpaOnnxVoiceActivityDetectorFront(vad);
            if (seg) {
                segments.push_back({seg->start, seg->n});
                SherpaOnnxDestroySpeechSegment(seg);
            }
            SherpaOnnxVoiceActivityDetectorPop(vad);
        }
    }
    SherpaOnnxVoiceActivityDetectorFlush(vad);
    while (!SherpaOnnxVoiceActivityDetectorEmpty(vad)) {
        const SherpaOnnxSpeechSegment *seg = SherpaOnnxVoiceActivityDetectorFront(vad);
        if (seg) {
            segments.push_back({seg->start, seg->n});
            SherpaOnnxDestroySpeechSegment(seg);
        }
        SherpaOnnxVoiceActivityDetectorPop(vad);
    }
    SherpaOnnxDestroyVoiceActivityDetector(vad);
    return segments;
}

struct MergedChunk {
    int start;
    int count;
};

// 2. Merge small VAD segments into target duration (target: 10s ~ 18s)
std::vector<MergedChunk> mergeSegments(const std::vector<RawVadSegment> &rawSegs,
                                       int sampleRate,
                                       int targetMinSec = 10,
                                       int targetMaxSec = 18)
{
    std::vector<MergedChunk> chunks;
    if (rawSegs.empty()) return chunks;

    const int targetMinSamples = targetMinSec * sampleRate;
    const int targetMaxSamples = targetMaxSec * sampleRate;

    int curStart = rawSegs.front().start;
    int curEnd = rawSegs.front().start + rawSegs.front().count;

    for (size_t i = 1; i < rawSegs.size(); ++i) {
        const auto &seg = rawSegs[i];
        const int nextEnd = seg.start + seg.count;
        const int combinedLength = nextEnd - curStart;

        // If current accumulated length is already >= targetMinSec and adding next exceeds targetMaxSec,
        // finish current chunk at natural pause
        if ((curEnd - curStart >= targetMinSamples) && (combinedLength > targetMaxSamples)) {
            chunks.push_back({curStart, curEnd - curStart});
            curStart = seg.start;
            curEnd = nextEnd;
        } else {
            // Merge into current chunk
            curEnd = nextEnd;
        }
    }
    if (curEnd > curStart) {
        chunks.push_back({curStart, curEnd - curStart});
    }

    return chunks;
}

// ── Baseline (Current TalkInput 15s Hard Split) ─────────────────────────
QString runBaseline(const talkinput::AsrPreset &preset,
                    const QByteArray &pcm16, int sampleRate, int channels)
{
    auto recognizerExpected = talkinput::SpeechRecognizer::createFromPreset(preset, nullptr, true);
    if (!recognizerExpected) return {};
    auto recognizer = std::move(*recognizerExpected);

    QString text;
    QObject::connect(recognizer.get(), &talkinput::SpeechRecognizer::resultChanged,
                     [&text](const QString &t, bool isFinal) {
                         if (isFinal) text = t;
                     });
    recognizer->acceptPcm16(pcm16, sampleRate, channels);
    recognizer->finish();
    return text;
}

// ── Smart Merged VAD Strategy (with configurable padding ms) ────────────
QString runSmartMergedVad(const talkinput::AsrPreset &preset,
                          const std::vector<float> &floats, int sampleRate,
                          int paddingMs)
{
    auto rawSegs = extractVadSegments(floats, sampleRate);
    if (rawSegs.empty()) return {};

    auto mergedChunks = mergeSegments(rawSegs, sampleRate, 10, 18);
    SPDLOG_INFO("  [SmartMerged VAD] Raw segments: {}, Merged into {} chunks (Target: 10~18s), Padding: {}ms",
                rawSegs.size(), mergedChunks.size(), paddingMs);

    const int totalSamples = static_cast<int>(floats.size());
    const int padSamples = sampleRate * paddingMs / 1000;
    QStringList transcript;

    for (size_t i = 0; i < mergedChunks.size(); ++i) {
        const auto &chunk = mergedChunks[i];
        const int paddedStart = std::max(0, chunk.start - padSamples);
        const int paddedEnd = std::min(totalSamples, chunk.start + chunk.count + padSamples);
        const int paddedCount = paddedEnd - paddedStart;

        QByteArray segPcm;
        segPcm.reserve(paddedCount * 2);
        for (int k = 0; k < paddedCount; ++k) {
            const float s = std::clamp(floats[static_cast<size_t>(paddedStart + k)], -1.0f, 1.0f);
            const qint16 sample = static_cast<qint16>(s * 32767.0f);
            segPcm.append(reinterpret_cast<const char *>(&sample), 2);
        }

        auto singleRecExpected = talkinput::SpeechRecognizer::createFromPreset(preset, nullptr, true);
        if (!singleRecExpected) continue;
        auto &singleRec = *singleRecExpected;

        QString piece;
        QObject::connect(singleRec.get(), &talkinput::SpeechRecognizer::resultChanged,
                         [&piece](const QString &t, bool isFinal) {
                             if (isFinal) piece = t;
                         });
        singleRec->acceptPcm16(segPcm, sampleRate, 1);
        singleRec->finish();

        if (!piece.isEmpty()) {
            transcript.append(piece);
        }
    }

    return transcript.join(QString());
}

void testEvaluation(const QString &presetId, const QString &audioPath)
{
    const auto &presets = talkinput::appConfig().asrPresets;
    const auto it = presets.find(presetId.toStdString());
    if (it == presets.end()) return;
    const auto &preset = it->second;

    SPDLOG_INFO("================================================================");
    SPDLOG_INFO("Testing: Model = {} ({}) | Audio = {}", preset.name, preset.id, audioPath.toStdString());
    SPDLOG_INFO("================================================================");

    auto decoded = talkinput::decodeAudioFileToPcm16(audioPath);
    if (!decoded) {
        SPDLOG_ERROR("Failed to decode audio: {}", decoded.error().toStdString());
        return;
    }
    const auto floats = pcm16ToFloats(decoded->pcm16, decoded->channels);

    // 1. Baseline
    auto t1_start = std::chrono::steady_clock::now();
    QString res1 = runBaseline(preset, decoded->pcm16, decoded->sampleRate, decoded->channels);
    auto t1_dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1_start).count();
    SPDLOG_INFO("--- [1. Baseline (Current 15s Hard Split)] ({} ms) ---\n{}\n", t1_dur, res1.toStdString());

    // 2. Smart Merged VAD (0ms Padding, Exact Natural Boundary)
    auto t2_start = std::chrono::steady_clock::now();
    QString res2 = runSmartMergedVad(preset, floats, decoded->sampleRate, 0);
    auto t2_dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t2_start).count();
    SPDLOG_INFO("--- [2. Smart Merged VAD (10~18s Chunk, 0ms Pad)] ({} ms) ---\n{}\n", t2_dur, res2.toStdString());

    // 3. Smart Merged VAD (100ms Padding on Boundaries)
    auto t3_start = std::chrono::steady_clock::now();
    QString res3 = runSmartMergedVad(preset, floats, decoded->sampleRate, 100);
    auto t3_dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t3_start).count();
    SPDLOG_INFO("--- [3. Smart Merged VAD (10~18s Chunk, +100ms Pad)] ({} ms) ---\n{}\n", t3_dur, res3.toStdString());

    // 4. Smart Merged VAD (200ms Padding on Boundaries)
    auto t4_start = std::chrono::steady_clock::now();
    QString res4 = runSmartMergedVad(preset, floats, decoded->sampleRate, 200);
    auto t4_dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t4_start).count();
    SPDLOG_INFO("--- [4. Smart Merged VAD (10~18s Chunk, +200ms Pad)] ({} ms) ---\n{}\n", t4_dur, res4.toStdString());
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString meetingPath = QDir::current().filePath("data/audio/meeting.m4a");
    const QString fiberartPath = QDir::current().filePath("data/audio/fiberart.m4a");

    talkinput::appConfig();

    // 1. SenseVoice
    testEvaluation(QStringLiteral("sense-voice-zh-en-ja-ko-yue-int8-2024-07-17"), fiberartPath);
    testEvaluation(QStringLiteral("sense-voice-zh-en-ja-ko-yue-int8-2024-07-17"), meetingPath);

    // 2. FunASR Nano
    testEvaluation(QStringLiteral("funasr-nano-int8-2025-12-30"), fiberartPath);
    testEvaluation(QStringLiteral("funasr-nano-int8-2025-12-30"), meetingPath);

    return 0;
}
