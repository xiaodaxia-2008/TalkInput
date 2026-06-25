#pragma once

#include "speech_recognizer.h"

#include <string>
#include <vector>

struct SherpaOnnxOfflineRecognizer;
struct SherpaOnnxOfflineRecognizerConfig;

namespace talkinput
{

class OfflineSpeechRecognizer : public SpeechRecognizer
{
public:
    explicit OfflineSpeechRecognizer(QObject *parent = nullptr);
    ~OfflineSpeechRecognizer() override;

    std::expected<void, QString>
    start() final;
    void stop() override;
    bool isRunning() const final;
    bool isStreaming() const final;

    void acceptPcm16(const QByteArray &audioData, int sampleRate,
                     int channelCount) final;
    void finish() final;
    void resetStream() final;

    int chunkSeconds() const { return m_chunkSeconds; }
    int maxChunkSeconds() const { return m_maxChunkSeconds; }

protected:
    OfflineSpeechRecognizer(QObject *parent, int chunkSeconds,
                            int maxChunkSeconds);

    virtual std::expected<void, QString>
    configureModel(SherpaOnnxOfflineRecognizerConfig *recognizer) = 0;

private:
    int findSplitBefore(int minPos, int maxPos) const;
    void decodeBlock(int start, int size);
    void flushCompletedChunks();

    const SherpaOnnxOfflineRecognizer *m_recognizer = nullptr;
    std::vector<float> m_samples;
    int m_modelSampleRate = 16000;
    int m_inputSampleRate = 0;
    QStringList m_transcript;
    bool m_processing = false;
    int m_chunkSeconds = 10;
    int m_maxChunkSeconds = 15;
};

} // namespace talkinput
