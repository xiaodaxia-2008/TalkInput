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

std::expected<void, QString> OnlineSpeechRecognizer::start()
{
    stop();

    const auto &params = m_preset.params;

    SherpaOnnxOnlineRecognizerConfig recognizerConfig;
    std::memset(&recognizerConfig, 0, sizeof(recognizerConfig));
    recognizerConfig.feat_config.sample_rate = params.sampleRate;
    recognizerConfig.feat_config.feature_dim = params.featureDim;

    auto modelResult = configureModel(&recognizerConfig);
    if (!modelResult) {
        stop();
        return std::unexpected(modelResult.error());
    }

    recognizerConfig.model_config.provider = "cpu";
    recognizerConfig.model_config.num_threads = std::max(1, params.numThreads);
    recognizerConfig.model_config.modeling_unit = params.modelingUnit.c_str();

    if (!m_preset.hotwordsText.empty()) {
        recognizerConfig.decoding_method = supportsModifiedBeamSearch()
                                               ? "modified_beam_search"
                                               : "greedy_search";
        recognizerConfig.hotwords_buf = m_preset.hotwordsText.c_str();
        recognizerConfig.hotwords_buf_size =
            static_cast<int32_t>(m_preset.hotwordsText.size());
        recognizerConfig.hotwords_score =
            static_cast<float>(params.hotwordsScore);
    }
    else {
        recognizerConfig.decoding_method = "greedy_search";
    }
    recognizerConfig.max_active_paths = 4;
    recognizerConfig.enable_endpoint = 0;

    m_recognizer = SherpaOnnxCreateOnlineRecognizer(&recognizerConfig);
    if (!m_recognizer) {
        stop();
        return std::unexpected(
            QStringLiteral("Failed to create online recognizer."));
    }

    m_stream = SherpaOnnxCreateOnlineStream(m_recognizer);
    if (!m_stream) {
        stop();
        return std::unexpected(
            QStringLiteral("Failed to create online stream."));
    }

    return {};
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
    const bool finalAlreadyPublished = decodePending();
    if (!publishResult(true) && !finalAlreadyPublished) {
        emit resultChanged(QString(), true);
    }
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

bool OnlineSpeechRecognizer::decodePending()
{
    while (SherpaOnnxIsOnlineStreamReady(m_recognizer, m_stream)) {
        SherpaOnnxDecodeOnlineStream(m_recognizer, m_stream);
    }

    publishResult(false);

    if (SherpaOnnxOnlineStreamIsEndpoint(m_recognizer, m_stream)) {
        const bool published = publishResult(true);
        SherpaOnnxOnlineStreamReset(m_recognizer, m_stream);
        m_lastText.clear();
        return published;
    }

    return false;
}

bool OnlineSpeechRecognizer::publishResult(bool isFinal)
{
    const SherpaOnnxOnlineRecognizerResult *result =
        SherpaOnnxGetOnlineStreamResult(m_recognizer, m_stream);
    if (!result) {
        return false;
    }

    QString text = decodeSherpaText(result->text);
    SherpaOnnxDestroyOnlineRecognizerResult(result);

    if (text.isEmpty()) {
        return false;
    }

    if (isFinal) {
        text = addPunctuation(text);
    }

    if (!isFinal && text == m_lastText) {
        return false;
    }

    m_lastText = text;
    emit resultChanged(text, isFinal);
    return true;
}

} // namespace talkinput
