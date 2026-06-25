#include "offline_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <cstring>
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

} // namespace

namespace talkinput
{

OfflineSpeechRecognizer::OfflineSpeechRecognizer(QObject *parent)
    : SpeechRecognizer(parent)
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
    m_modelingUnit = params.modelingUnit;
    config.model_config.modeling_unit = m_modelingUnit.c_str();

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

    m_tokensPath.clear();
    m_modelingUnit.clear();
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
}

void OfflineSpeechRecognizer::finish()
{
    if (!m_recognizer || m_samples.empty()) {
        return;
    }

    const int sampleRate = m_modelSampleRate;
    const int targetSamples = chunkSeconds() * sampleRate;
    const int maxSamples = targetSamples * 3 / 2;

    // ── 1. Detect silence split points ──
    // Frame-based RMS energy detection; split at the midpoint of any
    // continuous silence run ≥ 500 ms.
    constexpr int frameMs = 30;
    constexpr int minSilenceMs = 500;
    constexpr float silenceThresh = 0.005f;

    const int frameSize = sampleRate * frameMs / 1000;
    std::vector<int> splits;

    if (frameSize > 0 && static_cast<size_t>(frameSize) <= m_samples.size()) {
        int run = 0;
        size_t runStart = 0;
        for (size_t i = 0; i + frameSize <= m_samples.size(); i += frameSize) {
            float rms = 0.0f;
            for (int j = 0; j < frameSize; ++j) {
                rms += m_samples[i + j] * m_samples[i + j];
            }
            rms = std::sqrt(rms / static_cast<float>(frameSize));

            if (rms < silenceThresh) {
                if (run == 0) runStart = i;
                ++run;
            }
            else {
                if (run >= minSilenceMs / frameMs) {
                    splits.push_back(
                        static_cast<int>(runStart + (run * frameSize) / 2));
                }
                run = 0;
            }
        }
    }

    // ── 2. Pre-split into segments at silence boundaries ──
    // (start, size) pairs
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

    // ── 3. For any segment longer than maxSamples, split it evenly ──
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

    // ── 4. Greedy merge: accumulate segments until targetSamples ──
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

    // ── 5. Decode each merged block ──
    QStringList transcript;
    for (const auto &blk : blocks) {
        const SherpaOnnxOfflineStream *stream =
            SherpaOnnxCreateOfflineStream(m_recognizer);
        if (!stream) continue;

        SherpaOnnxAcceptWaveformOffline(stream, sampleRate,
                                        m_samples.data() + blk.start, blk.size);
        SherpaOnnxDecodeOfflineStream(m_recognizer, stream);

        const SherpaOnnxOfflineRecognizerResult *result =
            SherpaOnnxGetOfflineStreamResult(stream);
        if (result) {
            const QString text = decodeSherpaText(result->text);
            if (!text.isEmpty()) {
                transcript.append(text);
            }
            SherpaOnnxDestroyOfflineRecognizerResult(result);
        }
        SherpaOnnxDestroyOfflineStream(stream);
    }

    m_samples.clear();

    if (!transcript.isEmpty()) {
        const QString fullText = addPunctuation(transcript.join(QString()));
        emit resultChanged(fullText, true);
    }
}

void OfflineSpeechRecognizer::resetStream()
{
    m_samples.clear();
}

int OfflineSpeechRecognizer::chunkSeconds() const
{
    return 60;
}

} // namespace talkinput
