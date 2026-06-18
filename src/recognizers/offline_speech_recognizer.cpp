#include "offline_speech_recognizer.h"
#include "logging.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <QStringList>

#include <algorithm>
#include <cstdint>
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

bool OfflineSpeechRecognizer::start(const nlohmann::json &config,
                                    QString *errorMessage)
{
    stop();

    if (!prepareRecognizer(config, errorMessage)) {
        return false;
    }

    const nlohmann::json params =
        config.value("params", nlohmann::json::object());
    const int sampleRate = jsonInt(params, "sampleRate", 16000);

    SherpaOnnxOfflineRecognizerConfig recognizerConfig;
    std::memset(&recognizerConfig, 0, sizeof(recognizerConfig));
    recognizerConfig.feat_config.sample_rate = sampleRate;
    recognizerConfig.feat_config.feature_dim = jsonInt(params, "featureDim", 80);

    if (!configureModel(config, &recognizerConfig, errorMessage)) {
        stop();
        return false;
    }

    recognizerConfig.model_config.provider = "cpu";
    recognizerConfig.model_config.num_threads =
        std::max(1, jsonInt(params, "numThreads", 2));
    m_modelingUnit = jsonString(params, "modelingUnit", "cjkchar")
                         .toUtf8()
                         .toStdString();
    recognizerConfig.model_config.modeling_unit = m_modelingUnit.c_str();
    recognizerConfig.decoding_method = "greedy_search";
    recognizerConfig.max_active_paths = 4;

    m_recognizer = SherpaOnnxCreateOfflineRecognizer(&recognizerConfig);
    if (!m_recognizer) {
        stop();
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Failed to create offline recognizer.");
        }
        return false;
    }

    m_modelSampleRate = sampleRate;
    m_inputSampleRate = 0;
    m_samples.clear();
    return true;
}

void OfflineSpeechRecognizer::stop()
{
    if (m_recognizer) {
        SherpaOnnxDestroyOfflineRecognizer(m_recognizer);
        m_recognizer = nullptr;
    }

    m_samples.clear();
    m_inputSampleRate = 0;
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

    if (m_inputSampleRate == 0) {
        m_inputSampleRate = sampleRate;
    }
    else if (m_inputSampleRate != sampleRate) {
        SPDLOG_WARN("Offline ASR input sample rate changed from {} to {}; "
                    "keeping the first rate for this utterance",
                    m_inputSampleRate, sampleRate);
    }

    appendPcm16AsMonoFloat(audioData, channelCount, &m_samples);
}

void OfflineSpeechRecognizer::finish()
{
    if (!m_recognizer) {
        return;
    }

    decode();
}

void OfflineSpeechRecognizer::resetStream()
{
    m_samples.clear();
    m_inputSampleRate = 0;
}

int OfflineSpeechRecognizer::chunkSeconds() const
{
    return 60;
}

void OfflineSpeechRecognizer::decode()
{
    if (m_samples.empty()) {
        return;
    }

    const int inputSampleRate =
        m_inputSampleRate > 0 ? m_inputSampleRate : m_modelSampleRate;
    const int chunkSamples =
        std::max(1, inputSampleRate * std::max(1, chunkSeconds()));

    QStringList transcript;

    for (size_t off = 0; off < m_samples.size(); off += chunkSamples) {
        const int count = static_cast<int>(std::min(
            static_cast<size_t>(chunkSamples), m_samples.size() - off));
        if (count <= 0) {
            continue;
        }

        const SherpaOnnxOfflineStream *stream =
            SherpaOnnxCreateOfflineStream(m_recognizer);
        if (!stream) {
            continue;
        }

        SherpaOnnxAcceptWaveformOffline(stream, inputSampleRate,
                                        m_samples.data() + off, count);
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

    QString finalText = transcript.join("");
    finalText = addPunctuation(finalText);
    if (!finalText.isEmpty()) {
        emit resultChanged(finalText, true);
    }

    resetStream();
}

} // namespace talkinput
