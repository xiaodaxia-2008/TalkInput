#include "offline_speech_recognizer.h"

#include "audio_utils.h"
#include "utils.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <QDateTime>
#include <QDir>

#include <algorithm>
#include <climits>
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

namespace talkinput
{

// ── OfflineSpeechRecognizer ──────────────────────────────────────

OfflineSpeechRecognizer::OfflineSpeechRecognizer(QObject *parent)
    : OfflineSpeechRecognizer(parent, 10, 15)
{
}

OfflineSpeechRecognizer::OfflineSpeechRecognizer(QObject *parent,
                                                 int chunkSeconds,
                                                 int maxChunkSeconds)
    : SpeechRecognizer(parent), m_chunkSeconds(chunkSeconds),
      m_maxChunkSeconds(maxChunkSeconds)
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

    return {};
}

void OfflineSpeechRecognizer::stop()
{
    if (m_recognizer) {
        SherpaOnnxDestroyOfflineRecognizer(m_recognizer);
        m_recognizer = nullptr;
    }

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
        auto segs = segmentAudioBySilence(m_samples, m_modelSampleRate,
                                          m_maxChunkSeconds, m_chunkSeconds);
        for (const auto &seg : segs) {
            decodeBlock(seg.startSample, seg.sampleCount);
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

// ── Pseudo-online helpers ─────────────────────────────────────

int OfflineSpeechRecognizer::findSplitBefore(int minPos, int maxPos) const
{
    auto splits = talkinput::findSilenceSplits(
        {m_samples.data(), static_cast<size_t>(maxPos)}, m_modelSampleRate);
    auto it = std::find_if(splits.rbegin(), splits.rend(),
                           [minPos](int s) { return s >= minPos; });
    return it != splits.rend() ? *it : 0;
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

    const QString dir = QDir(talkinput::appDataDir()).filePath("asr_segments");
    QDir().mkpath(dir);
    const QString ts =
        QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz");
    const QString path =
        QDir(dir).filePath(QString("seg-%1-%2.wav")
                               .arg(ts)
                               .arg(m_segmentIndex++, 3, 10, QLatin1Char('0')));

    talkinput::savePcm16ToWav(pcm16, m_modelSampleRate, 1, path);
}

void OfflineSpeechRecognizer::flushCompletedChunks()
{
    if (m_processing || !m_recognizer) {
        return;
    }
    m_processing = true;

    const int targetSamples = m_chunkSeconds * m_modelSampleRate;
    const int hardLimit = m_maxChunkSeconds;

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

        m_samples.erase(m_samples.begin(), m_samples.begin() + split);

        if (!m_transcript.isEmpty()) {
            emit resultChanged(m_transcript.join(QString()), false);
        }
    }

    m_processing = false;
}

} // namespace talkinput
