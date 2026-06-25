#include "offline_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <numeric>
#include <span>
#include <vector>

namespace
{

std::vector<float> resampleFloats(const std::vector<float> &input,
                                  int inputRate, int outputRate)
{
    if (inputRate == outputRate || input.empty()) {
        return input;
    }

    const double ratio = static_cast<double>(outputRate) / inputRate;
    std::vector<float> output(static_cast<size_t>(input.size() * ratio));

    for (size_t i = 0; i < output.size(); ++i) {
        const double pos = static_cast<double>(i) / ratio;
        const size_t idx = static_cast<size_t>(pos);
        if (idx + 1 < input.size()) {
            const double frac = pos - idx;
            output[i] = static_cast<float>(
                input[idx] * (1.0 - frac) + input[idx + 1] * frac);
        }
        else {
            output[i] = input[idx];
        }
    }

    return output;
}

float computeFrameRms(std::span<const float> data)
{
    return std::sqrt(
        std::inner_product(data.begin(), data.end(), data.begin(), 0.0f)
        / data.size());
}

std::vector<int> silenceSplitPoints(std::span<const float> samples,
                                    int sampleRate,
                                    int frameMs = 30,
                                    int minSilenceMs = 500,
                                    float silenceThresh = 0.005f)
{
    const int frameSize = sampleRate * frameMs / 1000;
    const int maxPos = static_cast<int>(samples.size());
    if (frameSize <= 0 || frameSize > maxPos) return {};

    std::vector<int> splits;
    int run = 0, runStart = 0;

    for (int i = 0; i + frameSize <= maxPos; i += frameSize) {
        if (computeFrameRms(samples.subspan(i, frameSize)) < silenceThresh) {
            if (run == 0) runStart = i;
            ++run;
        }
        else {
            if (run >= minSilenceMs / frameMs)
                splits.push_back(runStart + (run * frameSize) / 2);
            run = 0;
        }
    }
    if (run >= minSilenceMs / frameMs)
        splits.push_back(runStart + (run * frameSize) / 2);

    return splits;
}

} // namespace

namespace talkinput
{

OfflineSpeechRecognizer::OfflineSpeechRecognizer(QObject *parent)
    : OfflineSpeechRecognizer(parent, 10, 15)
{
}

OfflineSpeechRecognizer::OfflineSpeechRecognizer(QObject *parent,
                                                 int chunkSeconds,
                                                 int maxChunkSeconds)
    : SpeechRecognizer(parent)
    , m_chunkSeconds(chunkSeconds)
    , m_maxChunkSeconds(maxChunkSeconds)
{
}

OfflineSpeechRecognizer::~OfflineSpeechRecognizer()
{
    stop();
}

std::expected<void, QString>
OfflineSpeechRecognizer::start()
{
    stop();

    auto prepResult = prepareRecognizer();
    if (!prepResult) {
        return std::unexpected(prepResult.error());
    }

    const auto &params = m_preset.params;

    SherpaOnnxOfflineRecognizerConfig config;
    std::memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = params.sampleRate;
    config.feat_config.feature_dim = params.featureDim;

    auto modelResult = configureModel(&config);
    if (!modelResult) {
        stop();
        return std::unexpected(modelResult.error());
    }

    config.model_config.provider = "cpu";
    config.model_config.num_threads = std::max(1, params.numThreads);
    config.model_config.debug = false;
    config.model_config.modeling_unit = params.modelingUnit.c_str();

    // Must be set explicitly — benchmark confirms NULL causes create to fail
    config.decoding_method = "greedy_search";
    config.max_active_paths = 4;

    m_recognizer = SherpaOnnxCreateOfflineRecognizer(&config);
    if (!m_recognizer) {
        stop();
        return std::unexpected(
            QStringLiteral("Failed to create offline recognizer."));
    }

    m_modelSampleRate = params.sampleRate;
    m_inputSampleRate = params.sampleRate;
    return {};
}

void OfflineSpeechRecognizer::stop()
{
    if (m_recognizer) {
        SherpaOnnxDestroyOfflineRecognizer(m_recognizer);
        m_recognizer = nullptr;
    }

    stopPunctuation();
}

bool OfflineSpeechRecognizer::isRunning() const
{
    return m_recognizer != nullptr;
}

bool OfflineSpeechRecognizer::isStreaming() const
{
    return false;
}

void OfflineSpeechRecognizer::acceptPcm16(const QByteArray &audioData,
                                          int sampleRate, int channelCount)
{
    if (!m_recognizer || audioData.isEmpty() || sampleRate <= 0 ||
        channelCount <= 0)
    {
        return;
    }

    std::vector<float> chunk;
    appendPcm16AsMonoFloat(audioData, channelCount, &chunk);

    if (sampleRate != m_modelSampleRate) {
        chunk = resampleFloats(chunk, sampleRate, m_modelSampleRate);
    }

    m_samples.insert(m_samples.end(), chunk.begin(), chunk.end());

    flushCompletedChunks();
}

void OfflineSpeechRecognizer::finish()
{
    flushCompletedChunks();

    if (!m_samples.empty() && m_recognizer) {
        const int sampleRate = m_modelSampleRate;
        const int targetSamples = chunkSeconds() * sampleRate;
        const int maxSeconds = std::min(maxChunkSeconds(), targetSamples * 6 / sampleRate);
        const int maxSamples = maxSeconds * sampleRate;

        // ── Silence detection on remainder ──
        auto splits = silenceSplitPoints(m_samples, sampleRate);

        // ── Pre-split ──
        struct Segment { int start; int size; };
        std::vector<Segment> raw;

        int prev = 0;
        for (int sp : splits) {
            sp = std::clamp(sp, prev, static_cast<int>(m_samples.size()));
            int sz = sp - prev;
            if (sz > 0) raw.push_back({prev, sz});
            prev = sp;
        }
        if (prev < static_cast<int>(m_samples.size())) {
            raw.push_back({prev, static_cast<int>(m_samples.size()) - prev});
        }
        if (raw.empty()) {
            raw.push_back({0, static_cast<int>(m_samples.size())});
        }

        // ── Oversize split ──
        std::vector<Segment> segs;
        for (const auto &seg : raw) {
            if (seg.size > maxSamples) {
                const int parts = (seg.size + maxSamples - 1) / maxSamples;
                const int base = seg.size / parts;
                for (int i = 0; i < parts; ++i) {
                    const int start = seg.start + i * base;
                    const int size =
                        (i == parts - 1) ? (seg.start + seg.size - start) : base;
                    segs.push_back({start, size});
                }
            }
            else {
                segs.push_back(seg);
            }
        }

        // ── Greedy merge ──
        std::vector<Segment> blocks;
        int accStart = segs[0].start;
        int accSize = segs[0].size;

        for (size_t i = 1; i < segs.size(); ++i) {
            if (accSize + segs[i].size <= targetSamples) {
                accSize += segs[i].size;
            }
            else {
                blocks.push_back({accStart, accSize});
                accStart = segs[i].start;
                accSize = segs[i].size;
            }
        }
        blocks.push_back({accStart, accSize});

        // ── Decode each block ──
        for (const auto &blk : blocks) {
            decodeBlock(blk.start, blk.size);
        }
    }

    m_samples.clear();

    if (!m_transcript.isEmpty()) {
        emit resultChanged(addPunctuation(m_transcript.join(QString())), true);
        m_transcript.clear();
    }
}

void OfflineSpeechRecognizer::resetStream()
{
    m_samples.clear();
    m_transcript.clear();
}

// ── Pseudo-online helpers ─────────────────────────────────────

int OfflineSpeechRecognizer::findSplitBefore(int minPos, int maxPos) const
{
    auto splits = silenceSplitPoints({m_samples.data(), static_cast<size_t>(maxPos)}, m_modelSampleRate);
    auto it = std::find_if(splits.rbegin(), splits.rend(),
                           [minPos](int s) { return s >= minPos; });
    return it != splits.rend() ? *it : 0;
}

void OfflineSpeechRecognizer::decodeBlock(int start, int size)
{
    const SherpaOnnxOfflineStream *stream =
        SherpaOnnxCreateOfflineStream(m_recognizer);
    if (!stream) return;

    SherpaOnnxAcceptWaveformOffline(stream, m_modelSampleRate,
                                    m_samples.data() + start, size);
    SherpaOnnxDecodeOfflineStream(m_recognizer, stream);

    const SherpaOnnxOfflineRecognizerResult *result =
        SherpaOnnxGetOfflineStreamResult(stream);
    if (result) {
        const QString text = decodeSherpaText(result->text);
        if (!text.isEmpty()) {
            m_transcript.append(text);
        }
        SherpaOnnxDestroyOfflineRecognizerResult(result);
    }
    SherpaOnnxDestroyOfflineStream(stream);
}

void OfflineSpeechRecognizer::flushCompletedChunks()
{
    if (m_processing || !m_recognizer) return;
    m_processing = true;

    const int targetSamples = chunkSeconds() * m_modelSampleRate;
    const int hardLimit = maxChunkSeconds();

    while (static_cast<int>(m_samples.size()) >= targetSamples) {
        const int searchEnd = hardLimit < INT_MAX
            ? std::min(hardLimit * m_modelSampleRate,
                       static_cast<int>(m_samples.size()))
            : static_cast<int>(m_samples.size());
        int split = findSplitBefore(targetSamples, searchEnd);

        if (split == 0) {
            if (hardLimit < INT_MAX) {
                split = targetSamples;
            }
            else {
                break;
            }
        }

        decodeBlock(0, split);

        m_samples.erase(m_samples.begin(),
                        m_samples.begin() + split);

        if (!m_transcript.isEmpty()) {
            emit resultChanged(m_transcript.join(QString()), false);
        }
    }

    m_processing = false;
}

} // namespace talkinput
