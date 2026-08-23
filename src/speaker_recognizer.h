#pragma once

#include <QObject>
#include <QString>

#include <expected>
#include <memory>
#include <span>
#include <vector>

struct SherpaOnnxSpeakerEmbeddingExtractor;
struct SherpaOnnxSpeakerEmbeddingManager;

namespace talkinput
{

struct DiarizedSegment
{
    int startMs = 0;
    int endMs = 0;
    int speakerId = 0;
    QString speakerName;
    QString text;
};

class SpeakerRecognizer : public QObject
{
    Q_OBJECT

public:
    explicit SpeakerRecognizer(QObject *parent = nullptr);
    ~SpeakerRecognizer() override;

    /// Initialize the extractor using the specified or default model path.
    std::expected<void, QString> init(const QString &modelPath = QString(),
                                      int numThreads = 2);

    bool isInitialized() const;
    int embeddingDim() const;

    /// Extract a normalized speaker embedding vector (e.g. 192/256 floats).
    std::vector<float> extractEmbedding(std::span<const float> samples,
                                        int sampleRate = 16000) const;

    /// Cosine similarity between two embedding vectors [-1.0, 1.0].
    static float computeSimilarity(std::span<const float> a,
                                   std::span<const float> b);

    /// Register/Enroll a known speaker with an embedding.
    bool registerSpeaker(const QString &name, const std::vector<float> &embedding);

    /// Identify which enrolled speaker best matches the embedding.
    std::pair<QString, float> identifySpeaker(const std::vector<float> &embedding,
                                             float threshold = 0.5F) const;

    /// Perform speaker diarization on raw mono audio.
    /// Clusters speech into speakers based on cosine similarity threshold.
    std::vector<DiarizedSegment>
    diarizeAudio(std::span<const float> samples, int sampleRate,
                 float similarityThreshold = 0.55F) const;

private:
    const SherpaOnnxSpeakerEmbeddingExtractor *m_extractor = nullptr;
    const SherpaOnnxSpeakerEmbeddingManager *m_manager = nullptr;
    int m_dim = 0;
};

} // namespace talkinput
