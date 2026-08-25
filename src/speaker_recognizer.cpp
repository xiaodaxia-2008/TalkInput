#include "speaker_recognizer.h"

#include "audio_utils.h"
#include "logging.h"
#include "utils.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{

float dotProduct(std::span<const float> a, std::span<const float> b)
{
    if (a.size() != b.size() || a.empty()) {
        return 0.0F;
    }
    float sum = 0.0F;
    for (size_t i = 0; i < a.size(); ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

float vectorNorm(std::span<const float> a)
{
    float sum = 0.0F;
    for (float v : a) {
        sum += v * v;
    }
    return std::sqrt(sum);
}

} // namespace

namespace zenny
{

SpeakerRecognizer::SpeakerRecognizer(QObject *parent)
    : QObject(parent)
{
}

SpeakerRecognizer::~SpeakerRecognizer()
{
    if (m_manager) {
        SherpaOnnxDestroySpeakerEmbeddingManager(m_manager);
        m_manager = nullptr;
    }
    if (m_extractor) {
        SherpaOnnxDestroySpeakerEmbeddingExtractor(m_extractor);
        m_extractor = nullptr;
    }
}

std::expected<void, QString> SpeakerRecognizer::init(const QString &modelPath,
                                                     int numThreads)
{
    QString resolvedPath = modelPath;
    if (resolvedPath.isEmpty()) {
        const QString relPath = QStringLiteral(
            "models/sherpa-onnx-campplus-zh-cn-16k-common/"
            "3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx");

        const QString appPath =
            QDir(QCoreApplication::applicationDirPath()).filePath(relPath);
        const QString dataPath = QDir(appDataDir()).filePath(relPath);
        const QString cwdPath = QDir::current().filePath(relPath);

        if (QFileInfo::exists(appPath)) {
            resolvedPath = appPath;
        }
        else if (QFileInfo::exists(dataPath)) {
            resolvedPath = dataPath;
        }
        else if (QFileInfo::exists(cwdPath)) {
            resolvedPath = cwdPath;
        }
    }

    if (resolvedPath.isEmpty() || !QFileInfo::exists(resolvedPath)) {
        return std::unexpected(QStringLiteral("Speaker embedding model not found: %1")
                                   .arg(resolvedPath.isEmpty() ? QStringLiteral("<default>") : resolvedPath));
    }

    SherpaOnnxSpeakerEmbeddingExtractorConfig config;
    std::memset(&config, 0, sizeof(config));
    const std::string pathStd = resolvedPath.toUtf8().toStdString();
    config.model = pathStd.c_str();
    config.num_threads = std::max(1, numThreads);
    config.debug = 0;
    config.provider = "cpu";

    m_extractor = SherpaOnnxCreateSpeakerEmbeddingExtractor(&config);
    if (!m_extractor) {
        return std::unexpected(QStringLiteral("Failed to create speaker embedding extractor."));
    }

    m_dim = SherpaOnnxSpeakerEmbeddingExtractorDim(m_extractor);
    m_manager = SherpaOnnxCreateSpeakerEmbeddingManager(m_dim);
    if (!m_manager) {
        SherpaOnnxDestroySpeakerEmbeddingExtractor(m_extractor);
        m_extractor = nullptr;
        return std::unexpected(QStringLiteral("Failed to create speaker embedding manager."));
    }

    SPDLOG_INFO("SpeakerRecognizer initialized: dim={}, model={}", m_dim, pathStd);
    return {};
}

bool SpeakerRecognizer::isInitialized() const
{
    return m_extractor != nullptr && m_manager != nullptr;
}

int SpeakerRecognizer::embeddingDim() const
{
    return m_dim;
}

std::vector<float>
SpeakerRecognizer::extractEmbedding(std::span<const float> samples,
                                    int sampleRate) const
{
    if (!m_extractor || samples.empty() || sampleRate <= 0) {
        return {};
    }

    const SherpaOnnxOnlineStream *stream =
        SherpaOnnxSpeakerEmbeddingExtractorCreateStream(m_extractor);
    if (!stream) {
        return {};
    }

    SherpaOnnxOnlineStreamAcceptWaveform(
        stream, sampleRate, samples.data(), static_cast<int32_t>(samples.size()));
    SherpaOnnxOnlineStreamInputFinished(stream);

    std::vector<float> embedding;
    if (SherpaOnnxSpeakerEmbeddingExtractorIsReady(m_extractor, stream)) {
        const float *emb =
            SherpaOnnxSpeakerEmbeddingExtractorComputeEmbedding(m_extractor, stream);
        if (emb) {
            embedding.assign(emb, emb + m_dim);
            SherpaOnnxSpeakerEmbeddingExtractorDestroyEmbedding(emb);
        }
    }

    SherpaOnnxDestroyOnlineStream(stream);
    return embedding;
}

float SpeakerRecognizer::computeSimilarity(std::span<const float> a,
                                           std::span<const float> b)
{
    if (a.size() != b.size() || a.empty()) {
        return 0.0F;
    }
    const float normA = vectorNorm(a);
    const float normB = vectorNorm(b);
    if (normA <= 1e-6F || normB <= 1e-6F) {
        return 0.0F;
    }
    return dotProduct(a, b) / (normA * normB);
}

bool SpeakerRecognizer::registerSpeaker(const QString &name,
                                        const std::vector<float> &embedding)
{
    if (!m_manager || embedding.size() != static_cast<size_t>(m_dim) ||
        name.isEmpty())
    {
        return false;
    }
    const std::string nameStd = name.toUtf8().toStdString();
    return SherpaOnnxSpeakerEmbeddingManagerAdd(m_manager, nameStd.c_str(),
                                                embedding.data()) != 0;
}

std::pair<QString, float>
SpeakerRecognizer::identifySpeaker(const std::vector<float> &embedding,
                                   float threshold) const
{
    if (!m_manager || embedding.size() != static_cast<size_t>(m_dim)) {
        return {};
    }

    const char *matchedName = SherpaOnnxSpeakerEmbeddingManagerSearch(
        m_manager, embedding.data(), threshold);
    if (!matchedName) {
        return {};
    }

    const QString result = QString::fromUtf8(matchedName);
    SherpaOnnxSpeakerEmbeddingManagerFreeSearch(matchedName);

    const int matchResult = SherpaOnnxSpeakerEmbeddingManagerVerify(
        m_manager, result.toUtf8().constData(), embedding.data(), threshold);
    float score = 0.0F;
    if (matchResult > 0) {
        score = threshold;
    }

    return {result, score};
}

std::vector<DiarizedSegment>
SpeakerRecognizer::diarizeAudio(std::span<const float> samples, int sampleRate,
                                float similarityThreshold) const
{
    std::vector<DiarizedSegment> result;
    if (!m_extractor || samples.empty() || sampleRate <= 0) {
        return result;
    }

    struct RawSeg
    {
        int start;
        int count;
    };
    std::vector<RawSeg> rawSegments;

    // 1. Try Silero VAD for natural utterance segmentation
    QString vadPath = QDir(QCoreApplication::applicationDirPath())
                          .filePath("models/sherpa-onnx-silero-vad/silero_vad.onnx");
    if (!QFileInfo::exists(vadPath)) {
        vadPath = QDir(appDataDir())
                      .filePath("models/sherpa-onnx-silero-vad/silero_vad.onnx");
    }
    if (!QFileInfo::exists(vadPath)) {
        vadPath = QDir::current().filePath(
            "models/sherpa-onnx-silero-vad/silero_vad.onnx");
    }

    if (QFileInfo::exists(vadPath) && sampleRate == 16000) {
        SherpaOnnxVadModelConfig vadConfig;
        std::memset(&vadConfig, 0, sizeof(vadConfig));
        const std::string vadStr = vadPath.toUtf8().toStdString();
        vadConfig.silero_vad.model = vadStr.c_str();
        vadConfig.silero_vad.threshold = 0.5F;
        vadConfig.silero_vad.min_silence_duration = 0.3F;
        vadConfig.silero_vad.min_speech_duration = 0.2F;
        vadConfig.silero_vad.window_size = 512;
        vadConfig.silero_vad.max_speech_duration = 15.0F;
        vadConfig.sample_rate = sampleRate;
        vadConfig.num_threads = 2;
        vadConfig.provider = "cpu";

        const auto *vad =
            SherpaOnnxCreateVoiceActivityDetector(&vadConfig, 30.0F);
        if (vad) {
            constexpr int windowSamples = 512;
            const int totalSamples = static_cast<int>(samples.size());
            for (int offset = 0; offset < totalSamples;
                 offset += windowSamples)
            {
                const int count =
                    std::min(windowSamples, totalSamples - offset);
                SherpaOnnxVoiceActivityDetectorAcceptWaveform(
                    vad, samples.data() + offset, count);
                while (!SherpaOnnxVoiceActivityDetectorEmpty(vad)) {
                    const auto *seg =
                        SherpaOnnxVoiceActivityDetectorFront(vad);
                    if (seg) {
                        rawSegments.push_back({seg->start, seg->n});
                        SherpaOnnxDestroySpeechSegment(seg);
                    }
                    SherpaOnnxVoiceActivityDetectorPop(vad);
                }
            }
            SherpaOnnxVoiceActivityDetectorFlush(vad);
            while (!SherpaOnnxVoiceActivityDetectorEmpty(vad)) {
                const auto *seg = SherpaOnnxVoiceActivityDetectorFront(vad);
                if (seg) {
                    rawSegments.push_back({seg->start, seg->n});
                    SherpaOnnxDestroySpeechSegment(seg);
                }
                SherpaOnnxVoiceActivityDetectorPop(vad);
            }
            SherpaOnnxDestroyVoiceActivityDetector(vad);
        }
    }

    if (rawSegments.empty()) {
        const auto segs =
            segmentAudioBySilence(samples, sampleRate, 8, 30, 300, 0.02F);
        for (const auto &s : segs) {
            rawSegments.push_back({s.startSample, s.sampleCount});
        }
    }

    if (rawSegments.empty()) {
        return result;
    }

    struct SpeakerCluster
    {
        int id;
        std::vector<float> centroid;
        int count = 0;
    };

    std::vector<SpeakerCluster> clusters;

    for (const auto &seg : rawSegments) {
        if (seg.count < sampleRate * 2 / 5) { // Skip < 400ms segments
            continue;
        }

        const auto segSpan = samples.subspan(
            static_cast<size_t>(seg.start), static_cast<size_t>(seg.count));
        const auto emb = extractEmbedding(segSpan, sampleRate);
        if (emb.empty()) {
            continue;
        }

        int bestClusterId = -1;
        float bestSim = -1.0F;

        for (const auto &cluster : clusters) {
            const float sim = computeSimilarity(emb, cluster.centroid);
            if (sim > bestSim) {
                bestSim = sim;
                bestClusterId = cluster.id;
            }
        }

        if (bestSim >= similarityThreshold && bestClusterId >= 0) {
            // Update cluster centroid
            auto &cluster = clusters[static_cast<size_t>(bestClusterId - 1)];
            for (size_t d = 0; d < emb.size(); ++d) {
                cluster.centroid[d] =
                    (cluster.centroid[d] * cluster.count + emb[d]) /
                    (cluster.count + 1);
            }
            cluster.count++;
        }
        else {
            // Create new speaker
            const int newId = static_cast<int>(clusters.size()) + 1;
            clusters.push_back({newId, emb, 1});
            bestClusterId = newId;
        }

        const int startMs = static_cast<int>(
            static_cast<int64_t>(seg.start) * 1000 / sampleRate);
        const int endMs = static_cast<int>(
            static_cast<int64_t>(seg.start + seg.count) * 1000 / sampleRate);

        DiarizedSegment diarized;
        diarized.startMs = startMs;
        diarized.endMs = endMs;
        diarized.speakerId = bestClusterId;
        diarized.speakerName = QStringLiteral("Speaker %1").arg(bestClusterId);
        result.push_back(diarized);
    }

    // Merge consecutive segments from the same speaker if gap is small (< 1.5s)
    if (result.size() > 1) {
        std::vector<DiarizedSegment> merged;
        merged.push_back(result.front());

        for (size_t i = 1; i < result.size(); ++i) {
            auto &last = merged.back();
            const auto &curr = result[i];
            if (last.speakerId == curr.speakerId &&
                (curr.startMs - last.endMs <= 1500))
            {
                last.endMs = curr.endMs;
            }
            else {
                merged.push_back(curr);
            }
        }
        result = std::move(merged);
    }

    return result;
}

} // namespace zenny
