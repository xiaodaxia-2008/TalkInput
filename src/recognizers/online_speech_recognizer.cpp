#include "online_speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace talkinput
{

OnlineSpeechRecognizer::OnlineSpeechRecognizer(QObject *parent)
    : SpeechRecognizer(parent)
{
}

OnlineSpeechRecognizer::~OnlineSpeechRecognizer()
{
    stop();
}

bool OnlineSpeechRecognizer::start(const nlohmann::json &config,
                                     QString *errorMessage)
{
    stop();

    if (!prepareRecognizer(config, errorMessage)) {
        return false;
    }

    const nlohmann::json params =
        config.value("params", nlohmann::json::object());

    SherpaOnnxOnlineRecognizerConfig recognizerConfig;
    std::memset(&recognizerConfig, 0, sizeof(recognizerConfig));
    recognizerConfig.feat_config.sample_rate = jsonInt(params, "sampleRate", 16000);
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

    const QString hotwordsText = jsonString(config, "hotwordsText");
    m_hotwordsText = hotwordsText.toUtf8().toStdString();
    if (!m_hotwordsText.empty()) {
        recognizerConfig.decoding_method = supportsModifiedBeamSearch()
                                               ? "modified_beam_search"
                                               : "greedy_search";
        recognizerConfig.hotwords_buf = m_hotwordsText.c_str();
        recognizerConfig.hotwords_buf_size =
            static_cast<int32_t>(m_hotwordsText.size());
        recognizerConfig.hotwords_score =
            jsonFloat(params, "hotwordsScore", 1.5F);
    }
    else {
        recognizerConfig.decoding_method = "greedy_search";
    }
    recognizerConfig.max_active_paths = 4;
    recognizerConfig.enable_endpoint = 0;

    m_recognizer = SherpaOnnxCreateOnlineRecognizer(&recognizerConfig);
    if (!m_recognizer) {
        stop();
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Failed to create online recognizer.");
        }
        return false;
    }

    m_stream = SherpaOnnxCreateOnlineStream(m_recognizer);
    if (!m_stream) {
        stop();
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create online stream.");
        }
        return false;
    }

    m_lastText.clear();
    return true;
}

bool OnlineSpeechRecognizer::supportsModifiedBeamSearch() const
{
    return true;
}

void OnlineSpeechRecognizer::stop()
{
    if (m_stream) {
        SherpaOnnxDestroyOnlineStream(m_stream);
        m_stream = nullptr;
    }
    if (m_recognizer) {
        SherpaOnnxDestroyOnlineRecognizer(m_recognizer);
        m_recognizer = nullptr;
    }

    m_lastText.clear();
    m_encoderPath.clear();
    m_decoderPath.clear();
    m_joinerPath.clear();
    m_tokensPath.clear();
    m_modelingUnit.clear();
    m_hotwordsText.clear();
    stopPunctuation();
}

bool OnlineSpeechRecognizer::isRunning() const
{
    return m_recognizer != nullptr;
}

bool OnlineSpeechRecognizer::isStreaming() const
{
    return true;
}

void OnlineSpeechRecognizer::acceptPcm16(const QByteArray &audioData,
                                         int sampleRate, int channelCount)
{
    if (!m_recognizer || !m_stream || audioData.isEmpty() || sampleRate <= 0 ||
        channelCount <= 0)
    {
        return;
    }

    std::vector<float> samples;
    if (appendPcm16AsMonoFloat(audioData, channelCount, &samples) <= 0) {
        return;
    }

    SherpaOnnxOnlineStreamAcceptWaveform(m_stream, sampleRate, samples.data(),
                                         static_cast<int32_t>(samples.size()));
    decodePending();
}

void OnlineSpeechRecognizer::finish()
{
    if (!m_recognizer || !m_stream) {
        return;
    }

    SherpaOnnxOnlineStreamInputFinished(m_stream);
    decodePending();
    publishResult(true);
}

void OnlineSpeechRecognizer::resetStream()
{
    if (!m_recognizer) {
        return;
    }

    if (m_stream) {
        SherpaOnnxDestroyOnlineStream(m_stream);
    }
    m_stream = SherpaOnnxCreateOnlineStream(m_recognizer);
    m_lastText.clear();
}

void OnlineSpeechRecognizer::decodePending()
{
    while (SherpaOnnxIsOnlineStreamReady(m_recognizer, m_stream)) {
        SherpaOnnxDecodeOnlineStream(m_recognizer, m_stream);
    }

    publishResult(false);

    if (SherpaOnnxOnlineStreamIsEndpoint(m_recognizer, m_stream)) {
        publishResult(true);
        SherpaOnnxOnlineStreamReset(m_recognizer, m_stream);
        m_lastText.clear();
    }
}

void OnlineSpeechRecognizer::publishResult(bool isFinal)
{
    const SherpaOnnxOnlineRecognizerResult *result =
        SherpaOnnxGetOnlineStreamResult(m_recognizer, m_stream);
    if (!result) {
        return;
    }

    QString text = decodeSherpaText(result->text);
    SherpaOnnxDestroyOnlineRecognizerResult(result);

    if (text.isEmpty()) {
        return;
    }

    if (isFinal) {
        text = addPunctuation(text);
    }

    if (!isFinal && text == m_lastText) {
        return;
    }

    m_lastText = text;
    emit resultChanged(text, isFinal);
}

} // namespace talkinput
