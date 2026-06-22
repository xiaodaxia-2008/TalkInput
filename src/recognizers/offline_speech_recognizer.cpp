#include "offline_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <cstring>

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
OfflineSpeechRecognizer::start(const AsrPreset &preset)
{
    stop();

    auto prepResult = prepareRecognizer(preset);
    if (!prepResult) {
        return std::unexpected(prepResult.error());
    }

    const auto &params = preset.params;

    SherpaOnnxOfflineRecognizerConfig config;
    std::memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = params.sampleRate;
    config.feat_config.feature_dim = params.featureDim;

    auto modelResult = configureModel(preset, &config);
    if (!modelResult) {
        stop();
        return std::unexpected(modelResult.error());
    }

    config.model_config.provider = "cpu";
    config.model_config.num_threads = std::max(1, params.numThreads);
    config.model_config.debug = false;
    m_modelingUnit = params.modelingUnit;
    config.model_config.modeling_unit = m_modelingUnit.c_str();

    m_recognizer = SherpaOnnxCreateOfflineRecognizer(&config);
    if (!m_recognizer) {
        stop();
        return std::unexpected(
            QStringLiteral("Failed to create offline recognizer."));
    }

    m_modelSampleRate = params.sampleRate;
    m_inputSampleRate = params.sampleRate;
    m_chunkSeconds = chunkSeconds();
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
    m_hotwordsText.clear();
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

    m_samplesBuffer.insert(m_samplesBuffer.end(),
                           reinterpret_cast<const float *>(
                               audioData.constData()),
                           reinterpret_cast<const float *>(
                               audioData.constData() + audioData.size()));
}

void OfflineSpeechRecognizer::finish()
{
    if (!m_recognizer || m_samplesBuffer.empty()) {
        return;
    }

    const SherpaOnnxOfflineRecognizerResult *result =
        SherpaOnnxCreateOfflineRecognizerResult();
    if (!result) {
        return;
    }

    SherpaOnnxDecodeOfflineRecognizer(m_recognizer, m_samplesBuffer.data(),
                                      static_cast<int32_t>(m_samplesBuffer.size()),
                                      m_modelSampleRate, result);

    QString text = decodeSherpaText(result->text);
    SherpaOnnxDestroyOfflineRecognizerResult(result);
    m_samplesBuffer.clear();

    if (!text.isEmpty()) {
        text = addPunctuation(text);
        emit resultChanged(text, true);
    }
}

void OfflineSpeechRecognizer::resetStream()
{
    m_samplesBuffer.clear();
}

int OfflineSpeechRecognizer::chunkSeconds() const
{
    return 10;
}

} // namespace talkinput
