#include "offline_speech_recognizer.h"

#include "audio_utils.h"
#include "logging.h"
#include "utils.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>

#include <algorithm>
#include <climits>
#include <cstring>
#include <vector>

namespace
{

constexpr int minSegmentSeconds = 10;
constexpr int vadPaddingMs = 100;

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
            output[i] = static_cast<float>(input[idx] * (1.0 - frac) +
                                           input[idx + 1] * frac);
        }
        else {
            output[i] = input[idx];
        }
    }

    return output;
}

} // namespace

namespace zenny
{

// ── OfflineSpeechRecognizer ──────────────────────────────────────

OfflineSpeechRecognizer::OfflineSpeechRecognizer(QObject *parent)
    : OfflineSpeechRecognizer(parent, 18)
{
}

OfflineSpeechRecognizer::OfflineSpeechRecognizer(QObject *parent,
                                                 int maxChunkSeconds)
    : SpeechRecognizer(parent), m_maxChunkSeconds(maxChunkSeconds)
{
}

OfflineSpeechRecognizer::~OfflineSpeechRecognizer()
{
    stop();
}

std::expected<void, QString> OfflineSpeechRecognizer::start()
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

    config.decoding_method = "greedy_search";
    config.max_active_paths = 4;

    m_recognizer = SherpaOnnxCreateOfflineRecognizer(&config);
    if (!m_recognizer) {
        stop();
        return std::unexpected(
            QStringLiteral("Failed to create offline recognizer."));
    }

    m_modelSampleRate = params.sampleRate;

    const QString vadModelName = QStringLiteral("sherpa-onnx-silero-vad");
    const QString vadFileName = QStringLiteral("silero_vad.onnx");
    const QString executableVadPath =
        QDir(QCoreApplication::applicationDirPath())
            .filePath(
                QStringLiteral("models/%1/%2").arg(vadModelName, vadFileName));
    const QString dataVadPath =
        QDir(appDataDir())
            .filePath(
                QStringLiteral("models/%1/%2").arg(vadModelName, vadFileName));
    const QString vadPath =
        QFileInfo::exists(executableVadPath) ? executableVadPath : dataVadPath;

    if (m_modelSampleRate == 16000 && QFileInfo::exists(vadPath)) {
        m_vadModelPath = vadPath.toStdString();

        SherpaOnnxVadModelConfig vadConfig;
        std::memset(&vadConfig, 0, sizeof(vadConfig));
        vadConfig.silero_vad.model = m_vadModelPath.c_str();
        vadConfig.silero_vad.threshold = 0.5F;
        vadConfig.silero_vad.min_silence_duration = 0.3F;
        vadConfig.silero_vad.min_speech_duration = 0.1F;
        vadConfig.silero_vad.window_size = 512;
        vadConfig.silero_vad.max_speech_duration =
            static_cast<float>(m_maxChunkSeconds * 2);
        vadConfig.sample_rate = m_modelSampleRate;
        vadConfig.num_threads = std::max(1, params.numThreads);
        vadConfig.provider = "cpu";

        m_vad = SherpaOnnxCreateVoiceActivityDetector(&vadConfig, 30.0F);
        if (!m_vad) {
            SPDLOG_WARN("Failed to create Silero VAD; using RMS segmentation");
            m_vadModelPath.clear();
        }
        else {
            SPDLOG_INFO("Silero VAD loaded: {}", m_vadModelPath);
        }
    }
    else {
        SPDLOG_WARN("Silero VAD model not found; using RMS segmentation");
    }

    return {};
}

void OfflineSpeechRecognizer::stop()
{
    if (m_recognizer) {
        SherpaOnnxDestroyOfflineRecognizer(m_recognizer);
        m_recognizer = nullptr;
    }
    if (m_vad) {
        SherpaOnnxDestroyVoiceActivityDetector(m_vad);
        m_vad = nullptr;
    }
    m_vadModelPath.clear();

    m_samples.clear();
    m_transcript.clear();
    m_processing = false;
    m_segmentIndex = 0;
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
    if (!m_recognizer) {
        emit resultChanged(QString(), true);
        return;
    }

    flushCompletedChunks();

    if (!m_samples.empty()) {
        const int totalSamples = static_cast<int>(m_samples.size());
        const int padSamples = m_modelSampleRate * vadPaddingMs / 1000;
        const int minSplitSamples =
            std::min(totalSamples, minSegmentSeconds * m_modelSampleRate);
        const int maxSamples = m_maxChunkSeconds * m_modelSampleRate;

        bool vadDecoded = false;
        if (m_vad) {
            QElapsedTimer vadTimer;
            vadTimer.start();
            const auto rawSegs =
                extractVadSegments({m_samples.data(), m_samples.size()});
            SPDLOG_INFO("Offline ASR VAD: {} ms, {} raw segments",
                        vadTimer.elapsed(), rawSegs.size());
            if (!rawSegs.empty()) {
                const auto merged =
                    mergeVadSegments(rawSegs, minSplitSamples, maxSamples);
                for (const auto &chunk : merged) {
                    const int paddedStart =
                        std::max(0, chunk.startSample - padSamples);
                    const int paddedEnd = std::min(
                        totalSamples,
                        chunk.startSample + chunk.sampleCount + padSamples);
                    decodeBlock(paddedStart, paddedEnd - paddedStart);
                }
                vadDecoded = true;
            }
        }

        if (!vadDecoded) {
            const auto segs = segmentAudioBySilence(
                m_samples, m_modelSampleRate, m_maxChunkSeconds);
            for (const auto &seg : segs) {
                decodeBlock(seg.startSample, seg.sampleCount);
            }
        }
    }

    m_samples.clear();

    const QString finalText =
        m_transcript.isEmpty() ? QString()
                               : addPunctuation(m_transcript.join(QString()));
    m_transcript.clear();
    emit resultChanged(finalText, true);
}

void OfflineSpeechRecognizer::resetStream()
{
    m_samples.clear();
    m_transcript.clear();
    m_processing = false;
    m_segmentIndex = 0;
}

QString OfflineSpeechRecognizer::normalizeResultText(const QString &text) const
{
    return text;
}

// ── Pseudo-online helpers ─────────────────────────────────────

std::vector<OfflineSpeechRecognizer::VadSegment>
OfflineSpeechRecognizer::extractVadSegments(
    std::span<const float> samples) const
{
    if (!m_vad || samples.empty()) {
        return {};
    }

    constexpr int vadWindowSamples = 512;
    SherpaOnnxVoiceActivityDetectorReset(m_vad);

    std::vector<VadSegment> segments;
    const int totalSamples = static_cast<int>(samples.size());

    for (int offset = 0; offset < totalSamples; offset += vadWindowSamples) {
        const int count = std::min(vadWindowSamples, totalSamples - offset);
        SherpaOnnxVoiceActivityDetectorAcceptWaveform(
            m_vad, samples.data() + offset, count);

        while (!SherpaOnnxVoiceActivityDetectorEmpty(m_vad)) {
            const SherpaOnnxSpeechSegment *seg =
                SherpaOnnxVoiceActivityDetectorFront(m_vad);
            if (seg) {
                segments.push_back({seg->start, seg->n});
                SherpaOnnxDestroySpeechSegment(seg);
            }
            SherpaOnnxVoiceActivityDetectorPop(m_vad);
        }
    }

    SherpaOnnxVoiceActivityDetectorFlush(m_vad);
    while (!SherpaOnnxVoiceActivityDetectorEmpty(m_vad)) {
        const SherpaOnnxSpeechSegment *seg =
            SherpaOnnxVoiceActivityDetectorFront(m_vad);
        if (seg) {
            segments.push_back({seg->start, seg->n});
            SherpaOnnxDestroySpeechSegment(seg);
        }
        SherpaOnnxVoiceActivityDetectorPop(m_vad);
    }
    SherpaOnnxVoiceActivityDetectorReset(m_vad);

    return segments;
}

std::vector<AudioSegment> OfflineSpeechRecognizer::mergeVadSegments(
    const std::vector<VadSegment> &rawSegs, int minSamples,
    int maxSamples) const
{
    std::vector<AudioSegment> chunks;
    if (rawSegs.empty()) {
        return chunks;
    }

    int curStart = rawSegs.front().start;
    int curEnd = rawSegs.front().start + rawSegs.front().count;

    for (size_t i = 1; i < rawSegs.size(); ++i) {
        const auto &seg = rawSegs[i];
        const int nextEnd = seg.start + seg.count;
        const int combinedLength = nextEnd - curStart;

        if ((curEnd - curStart >= minSamples) && (combinedLength > maxSamples))
        {
            chunks.push_back({curStart, curEnd - curStart});
            curStart = seg.start;
            curEnd = nextEnd;
        }
        else {
            curEnd = nextEnd;
        }
    }
    if (curEnd > curStart) {
        chunks.push_back({curStart, curEnd - curStart});
    }

    return chunks;
}

int OfflineSpeechRecognizer::findSplitBefore(int minPos, int maxPos) const
{
    const int sampleCount =
        std::min(maxPos, m_samples.size() > static_cast<size_t>(INT_MAX)
                             ? INT_MAX
                             : static_cast<int>(m_samples.size()));
    const int padSamples = m_modelSampleRate * vadPaddingMs / 1000;

    if (m_vad) {
        const auto segments = extractVadSegments(
            {m_samples.data(), static_cast<size_t>(sampleCount)});
        int latestSpeechEnd = 0;
        for (const auto &seg : segments) {
            const int speechEnd = seg.start + seg.count;
            if (speechEnd >= minPos && speechEnd + padSamples <= sampleCount) {
                latestSpeechEnd = std::max(latestSpeechEnd, speechEnd);
            }
        }

        if (latestSpeechEnd > 0) {
            return std::min(sampleCount, latestSpeechEnd + padSamples);
        }
    }

    return zenny::findBestSilenceSplit(
        {m_samples.data(), static_cast<size_t>(maxPos)}, m_modelSampleRate,
        minPos, maxPos);
}

void OfflineSpeechRecognizer::decodeBlock(int start, int size)
{
    const SherpaOnnxOfflineStream *stream =
        SherpaOnnxCreateOfflineStream(m_recognizer);
    if (!stream) {
        return;
    }

    SherpaOnnxAcceptWaveformOffline(stream, m_modelSampleRate,
                                    m_samples.data() + start, size);
    QElapsedTimer decodeTimer;
    decodeTimer.start();
    SherpaOnnxDecodeOfflineStream(m_recognizer, stream);
    SPDLOG_INFO("Offline ASR decode: start={}, samples={} ({:.2f}s), {} ms",
                start, size, static_cast<double>(size) / m_modelSampleRate,
                decodeTimer.elapsed());

    const SherpaOnnxOfflineRecognizerResult *result =
        SherpaOnnxGetOfflineStreamResult(stream);
    if (result) {
        const QString text =
            normalizeResultText(decodeSherpaText(result->text));
        if (!text.isEmpty()) {
            m_transcript.append(text);
        }
        SherpaOnnxDestroyOfflineRecognizerResult(result);
    }
    SherpaOnnxDestroyOfflineStream(stream);

    saveSegment(start, size);
}

void OfflineSpeechRecognizer::saveSegment(int start, int size)
{
    if (size <= 0) {
        return;
    }

    QByteArray pcm16;
    pcm16.reserve(size * 2);
    for (int i = 0; i < size; ++i) {
        const float clamped =
            std::clamp(m_samples[static_cast<size_t>(start) + i], -1.0f, 1.0f);
        const qint16 sample = static_cast<qint16>(clamped * 32767.0f);
        pcm16.append(reinterpret_cast<const char *>(&sample), 2);
    }

    const QString dir = QDir(zenny::appDataDir()).filePath("asr_segments");
    QDir().mkpath(dir);
    const QString ts =
        QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz");
    const QString path =
        QDir(dir).filePath(QString("seg-%1-%2.wav")
                               .arg(ts)
                               .arg(m_segmentIndex++, 3, 10, QLatin1Char('0')));

    zenny::savePcm16ToWav(pcm16, m_modelSampleRate, 1, path);
}

void OfflineSpeechRecognizer::flushCompletedChunks()
{
    if (m_processing || !m_recognizer) {
        return;
    }
    m_processing = true;

    const qint64 maxSamples64 =
        static_cast<qint64>(m_maxChunkSeconds) * m_modelSampleRate;
    if (maxSamples64 <= 0) {
        m_processing = false;
        return;
    }
    const int maxSamples =
        maxSamples64 > INT_MAX ? INT_MAX : static_cast<int>(maxSamples64);
    const int minSplitSamples =
        std::min(maxSamples, minSegmentSeconds * m_modelSampleRate);
    while (m_samples.size() > static_cast<size_t>(maxSamples)) {
        const int searchEnd = maxSamples;
        int split = findSplitBefore(minSplitSamples, searchEnd);

        if (split == 0) {
            split = searchEnd;
        }

        decodeBlock(0, split);

        m_samples.erase(m_samples.begin(), m_samples.begin() + split);

        if (!m_transcript.isEmpty()) {
            emit resultChanged(m_transcript.join(QString()), false);
        }
    }

    m_processing = false;
}

} // namespace zenny
