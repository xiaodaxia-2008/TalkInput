#pragma once

#include "speech_recognizer.h"

#include <QStringList>

#include <span>
#include <string>
#include <vector>

struct SherpaOnnxOfflineRecognizer;
struct SherpaOnnxOfflineRecognizerConfig;
struct SherpaOnnxVoiceActivityDetector;

namespace zenny
{

struct AudioSegment;

class OfflineSpeechRecognizer : public SpeechRecognizer
{
public:
    explicit OfflineSpeechRecognizer(QObject *parent = nullptr);
    ~OfflineSpeechRecognizer() override;

    std::expected<void, QString> start() final;
    void stop() override;
    bool isRunning() const final;
    bool isStreaming() const final;

    void acceptPcm16(const QByteArray &audioData, int sampleRate,
                     int channelCount) final;
    void finish() final;
    void resetStream() final;

    int maxChunkSeconds() const
    {
        return m_maxChunkSeconds;
    }

protected:
    OfflineSpeechRecognizer(QObject *parent, int maxChunkSeconds);

    virtual std::expected<void, QString>
    configureModel(SherpaOnnxOfflineRecognizerConfig *recognizer) = 0;
    virtual QString normalizeResultText(const QString &text) const;

private:
    struct VadSegment
    {
        int start;
        int count;
    };

    std::vector<VadSegment>
    extractVadSegments(std::span<const float> samples) const;
    std::vector<AudioSegment>
    mergeVadSegments(const std::vector<VadSegment> &rawSegs, int minSamples,
                     int maxSamples) const;

    int findSplitBefore(int minPos, int maxPos) const;
    void decodeBlock(int start, int size);
    void saveSegment(int start, int size);
    void flushCompletedChunks();

    const SherpaOnnxOfflineRecognizer *m_recognizer = nullptr;
    const SherpaOnnxVoiceActivityDetector *m_vad = nullptr;
    std::string m_vadModelPath;
    std::vector<float> m_samples;
    QStringList m_transcript;
    int m_modelSampleRate = 16000;
    int m_maxChunkSeconds = 18;
    bool m_processing = false;
    int m_segmentIndex = 0;
};

} // namespace zenny
