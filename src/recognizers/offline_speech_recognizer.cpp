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

    const SherpaOnnxOfflineStream *stream =
        SherpaOnnxCreateOfflineStream(m_recognizer);
    if (!stream) {
        return;
    }

    SherpaOnnxAcceptWaveformOffline(stream, m_modelSampleRate, m_samples.data(),
                                    static_cast<int32_t>(m_samples.size()));

    SherpaOnnxDecodeOfflineStream(m_recognizer, stream);

    const SherpaOnnxOfflineRecognizerResult *result =
        SherpaOnnxGetOfflineStreamResult(stream);
    if (result) {
        QString text = decodeSherpaText(result->text);
        SherpaOnnxDestroyOfflineRecognizerResult(result);

        if (!text.isEmpty()) {
            text = addPunctuation(text);
            emit resultChanged(text, true);
        }
    }

    SherpaOnnxDestroyOfflineStream(stream);
    m_samples.clear();
}

void OfflineSpeechRecognizer::resetStream()
{
    m_samples.clear();
}

int OfflineSpeechRecognizer::chunkSeconds() const
{
    return 10;
}

} // namespace talkinput
