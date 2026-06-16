#include "speech_recognizer.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <QDir>
#include <QFileInfo>
#include <QtEndian>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

QString modelPath(const QString &modelDir, const QString &fileName)
{
    return QDir(modelDir).filePath(fileName);
}

bool fileExists(const QString &path, QString *errorMessage)
{
    const QFileInfo info(path);
    if (info.exists() && info.isFile()) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Required model file is missing: %1")
                            .arg(QDir::toNativeSeparators(path));
    }
    return false;
}

QString decodeSherpaText(const char *text)
{
    return QString::fromUtf8(text ? text : "").trimmed();
}

} // namespace

namespace talkinput
{

class SpeechRecognizer::Impl
{
public:
    bool start(const Config &config, QString *errorMessage);
    void stop();
    bool isRunning() const;
    void acceptPcm16(const QByteArray &audioData, int sampleRate,
                     int channelCount, SpeechRecognizer *owner);
    void finish(SpeechRecognizer *owner);

private:
    void decodePending(SpeechRecognizer *owner);
    void publishResult(bool isFinal, SpeechRecognizer *owner);

    const SherpaOnnxOnlineRecognizer *m_recognizer = nullptr;
    const SherpaOnnxOnlineStream *m_stream = nullptr;
    QString m_lastText;

    std::string m_encoderPath;
    std::string m_decoderPath;
    std::string m_joinerPath;
    std::string m_tokensPath;
};

SpeechRecognizer::SpeechRecognizer(QObject *parent)
    : QObject(parent)
    , m_impl(new Impl())
{
}

SpeechRecognizer::~SpeechRecognizer()
{
    stop();
    delete m_impl;
}

bool SpeechRecognizer::start(const Config &config, QString *errorMessage)
{
    return m_impl->start(config, errorMessage);
}

void SpeechRecognizer::stop()
{
    m_impl->stop();
}

bool SpeechRecognizer::isRunning() const
{
    return m_impl->isRunning();
}

void SpeechRecognizer::acceptPcm16(const QByteArray &audioData, int sampleRate,
                                   int channelCount)
{
    m_impl->acceptPcm16(audioData, sampleRate, channelCount, this);
}

void SpeechRecognizer::finish()
{
    m_impl->finish(this);
}

bool SpeechRecognizer::Impl::start(const Config &config, QString *errorMessage)
{
    stop();

    if (config.modelDir.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Model directory is empty.");
        }
        return false;
    }

    const QString encoder = modelPath(config.modelDir, config.encoderFile);
    const QString decoder = modelPath(config.modelDir, config.decoderFile);
    const QString joiner = modelPath(config.modelDir, config.joinerFile);
    const QString tokens = modelPath(config.modelDir, config.tokensFile);

    if (!fileExists(encoder, errorMessage) ||
        !fileExists(decoder, errorMessage) ||
        !fileExists(joiner, errorMessage) ||
        !fileExists(tokens, errorMessage)) {
        return false;
    }

    m_encoderPath = encoder.toUtf8().toStdString();
    m_decoderPath = decoder.toUtf8().toStdString();
    m_joinerPath = joiner.toUtf8().toStdString();
    m_tokensPath = tokens.toUtf8().toStdString();

    SherpaOnnxOnlineRecognizerConfig recognizerConfig;
    std::memset(&recognizerConfig, 0, sizeof(recognizerConfig));
    recognizerConfig.feat_config.sample_rate = config.sampleRate;
    recognizerConfig.feat_config.feature_dim = config.featureDim;
    recognizerConfig.model_config.transducer.encoder = m_encoderPath.c_str();
    recognizerConfig.model_config.transducer.decoder = m_decoderPath.c_str();
    recognizerConfig.model_config.transducer.joiner = m_joinerPath.c_str();
    recognizerConfig.model_config.tokens = m_tokensPath.c_str();
    recognizerConfig.model_config.provider = "cpu";
    const std::string modelingUnit = config.modelingUnit.toUtf8().toStdString();
    recognizerConfig.model_config.modeling_unit = modelingUnit.c_str();
    recognizerConfig.model_config.num_threads =
        std::max(1, config.numThreads);
    const std::string hotwordsText = config.hotwordsText.toUtf8().toStdString();
    if (!hotwordsText.empty()) {
        recognizerConfig.decoding_method = "modified_beam_search";
        recognizerConfig.hotwords_buf = hotwordsText.c_str();
        recognizerConfig.hotwords_buf_size =
            static_cast<int32_t>(hotwordsText.size());
        recognizerConfig.hotwords_score = config.hotwordsScore;
    }
    else {
        recognizerConfig.decoding_method = "greedy_search";
    }
    recognizerConfig.max_active_paths = 4;
    recognizerConfig.enable_endpoint = 1;
    recognizerConfig.rule1_min_trailing_silence = 2.4F;
    recognizerConfig.rule2_min_trailing_silence = 1.2F;
    recognizerConfig.rule3_min_utterance_length = 20.0F;

    m_recognizer = SherpaOnnxCreateOnlineRecognizer(&recognizerConfig);
    if (!m_recognizer) {
        stop();
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Failed to create sherpa-onnx recognizer.");
        }
        return false;
    }

    m_stream = SherpaOnnxCreateOnlineStream(m_recognizer);
    if (!m_stream) {
        stop();
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Failed to create sherpa-onnx stream.");
        }
        return false;
    }

    m_lastText.clear();
    return true;
}

void SpeechRecognizer::Impl::stop()
{
    if (m_stream) {
        SherpaOnnxDestroyOnlineStream(m_stream);
    }
    m_stream = nullptr;

    if (m_recognizer) {
        SherpaOnnxDestroyOnlineRecognizer(m_recognizer);
    }
    m_recognizer = nullptr;
    m_lastText.clear();
}

bool SpeechRecognizer::Impl::isRunning() const
{
    return m_recognizer && m_stream;
}

void SpeechRecognizer::Impl::acceptPcm16(const QByteArray &audioData,
                                         int sampleRate, int channelCount,
                                         SpeechRecognizer *owner)
{
    if (!isRunning() || audioData.isEmpty() || sampleRate <= 0 ||
        channelCount <= 0) {
        return;
    }

    constexpr int bytesPerSample = 2;
    const int bytesPerFrame = bytesPerSample * channelCount;
    const int frameCount = audioData.size() / bytesPerFrame;
    if (frameCount <= 0) {
        return;
    }

    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(frameCount));

    const auto *data =
        reinterpret_cast<const uchar *>(audioData.constData());
    for (int frame = 0; frame < frameCount; ++frame) {
        float mixed = 0.0F;
        const int frameOffset = frame * bytesPerFrame;
        for (int channel = 0; channel < channelCount; ++channel) {
            const int sampleOffset = frameOffset + channel * bytesPerSample;
            const qint16 sample =
                qFromLittleEndian<qint16>(data + sampleOffset);
            mixed += static_cast<float>(sample) / 32768.0F;
        }
        samples.push_back(mixed / static_cast<float>(channelCount));
    }

    SherpaOnnxOnlineStreamAcceptWaveform(
        m_stream, sampleRate, samples.data(),
        static_cast<int32_t>(samples.size()));
    decodePending(owner);
}

void SpeechRecognizer::Impl::finish(SpeechRecognizer *owner)
{
    if (!isRunning()) {
        return;
    }

    SherpaOnnxOnlineStreamInputFinished(m_stream);
    decodePending(owner);
    publishResult(true, owner);
}

void SpeechRecognizer::Impl::decodePending(SpeechRecognizer *owner)
{
    while (SherpaOnnxIsOnlineStreamReady(m_recognizer, m_stream)) {
        SherpaOnnxDecodeOnlineStream(m_recognizer, m_stream);
    }

    publishResult(false, owner);

    if (SherpaOnnxOnlineStreamIsEndpoint(m_recognizer, m_stream)) {
        publishResult(true, owner);
        SherpaOnnxOnlineStreamReset(m_recognizer, m_stream);
        m_lastText.clear();
    }
}

void SpeechRecognizer::Impl::publishResult(bool isFinal,
                                           SpeechRecognizer *owner)
{
    const SherpaOnnxOnlineRecognizerResult *result =
        SherpaOnnxGetOnlineStreamResult(m_recognizer, m_stream);
    if (!result) {
        return;
    }

    const QString text = decodeSherpaText(result->text);
    SherpaOnnxDestroyOnlineRecognizerResult(result);

    if (text.isEmpty()) {
        return;
    }

    if (!isFinal && text == m_lastText) {
        return;
    }

    m_lastText = text;
    emit owner->resultChanged(text, isFinal);
    emit owner->logMessage(
        QStringLiteral("%1: %2")
            .arg(isFinal ? QStringLiteral("Final result")
                         : QStringLiteral("Partial result"),
                 text));
}

} // namespace talkinput
