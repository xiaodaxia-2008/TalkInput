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

SpeechRecognizer::SpeechRecognizer(QObject *parent)
    : QObject(parent)
{
}

SpeechRecognizer::~SpeechRecognizer()
{
    stop();
}

bool SpeechRecognizer::start(const Config &config, QString *errorMessage)
{
    stop();

    if (config.modelDir.trimmed().isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Model directory is empty.");
        return false;
    }

    // Initialize punctuation model if path is provided
    if (!config.punctuationModelPath.isEmpty()) {
        const QString punctPath = config.punctuationModelPath;
        if (QFileInfo::exists(punctPath)) {
            SherpaOnnxOfflinePunctuationConfig punctConfig;
            std::memset(&punctConfig, 0, sizeof(punctConfig));
            const std::string punctModelStr = punctPath.toUtf8().toStdString();
            punctConfig.model.ct_transformer = punctModelStr.c_str();
            punctConfig.model.num_threads = std::max(1, config.numThreads);
            punctConfig.model.provider = "cpu";
            m_punct = SherpaOnnxCreateOfflinePunctuation(&punctConfig);
            if (!m_punct)
                qWarning().noquote() << "Failed to create punctuation processor";
            else
                qInfo().noquote() << "Punctuation model loaded:" << punctPath;
        } else {
            qWarning().noquote() << "Punctuation model not found:" << punctPath;
        }
    }

    if (config.type == Type::StreamingTransducer ||
        config.type == Type::StreamingParaformer) {
        return startOnline(config, errorMessage);
    }
    return startOffline(config, errorMessage);
}

bool SpeechRecognizer::startOnline(const Config &config, QString *errorMessage)
{
    const QString encoder = modelPath(config.modelDir, config.encoderFile);
    const QString decoder = modelPath(config.modelDir, config.decoderFile);
    const QString tokens = modelPath(config.modelDir, config.tokensFile);

    if (config.type == Type::StreamingTransducer) {
        const QString joiner = modelPath(config.modelDir, config.joinerFile);
        if (!fileExists(encoder, errorMessage) ||
            !fileExists(decoder, errorMessage) ||
            !fileExists(joiner, errorMessage) ||
            !fileExists(tokens, errorMessage))
            return false;
    } else {
        if (!fileExists(encoder, errorMessage) ||
            !fileExists(decoder, errorMessage) ||
            !fileExists(tokens, errorMessage))
            return false;
    }

    m_online.encoderPath = encoder.toUtf8().toStdString();
    m_online.decoderPath = decoder.toUtf8().toStdString();
    m_online.tokensPath = tokens.toUtf8().toStdString();

    SherpaOnnxOnlineRecognizerConfig recognizerConfig;
    std::memset(&recognizerConfig, 0, sizeof(recognizerConfig));
    recognizerConfig.feat_config.sample_rate = config.sampleRate;
    recognizerConfig.feat_config.feature_dim = config.featureDim;

    if (config.type == Type::StreamingTransducer) {
        const QString joiner = modelPath(config.modelDir, config.joinerFile);
        m_online.joinerPath = joiner.toUtf8().toStdString();
        recognizerConfig.model_config.transducer.encoder = m_online.encoderPath.c_str();
        recognizerConfig.model_config.transducer.decoder = m_online.decoderPath.c_str();
        recognizerConfig.model_config.transducer.joiner = m_online.joinerPath.c_str();
    } else {
        recognizerConfig.model_config.paraformer.encoder = m_online.encoderPath.c_str();
        recognizerConfig.model_config.paraformer.decoder = m_online.decoderPath.c_str();
    }

    recognizerConfig.model_config.tokens = m_online.tokensPath.c_str();
    recognizerConfig.model_config.provider = "cpu";
    recognizerConfig.model_config.num_threads = std::max(1, config.numThreads);

    const std::string modelingUnit = config.modelingUnit.toUtf8().toStdString();
    recognizerConfig.model_config.modeling_unit = modelingUnit.c_str();

    const std::string hotwordsText = config.hotwordsText.toUtf8().toStdString();
    if (!hotwordsText.empty()) {
        recognizerConfig.decoding_method = "modified_beam_search";
        recognizerConfig.hotwords_buf = hotwordsText.c_str();
        recognizerConfig.hotwords_buf_size =
            static_cast<int32_t>(hotwordsText.size());
        recognizerConfig.hotwords_score = config.hotwordsScore;
    } else {
        recognizerConfig.decoding_method = "greedy_search";
    }
    recognizerConfig.max_active_paths = 4;
    recognizerConfig.enable_endpoint = 0;

    m_online.recognizer = SherpaOnnxCreateOnlineRecognizer(&recognizerConfig);
    if (!m_online.recognizer) {
        stop();
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to create online recognizer.");
        return false;
    }

    m_online.stream = SherpaOnnxCreateOnlineStream(m_online.recognizer);
    if (!m_online.stream) {
        stop();
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to create online stream.");
        return false;
    }

    m_online.lastText.clear();
    return true;
}

bool SpeechRecognizer::startOffline(const Config &config, QString *errorMessage)
{
    const QString tokens = modelPath(config.modelDir, config.tokensFile);

    SherpaOnnxOfflineRecognizerConfig recognizerConfig;
    std::memset(&recognizerConfig, 0, sizeof(recognizerConfig));
    recognizerConfig.feat_config.sample_rate = config.sampleRate;
    recognizerConfig.feat_config.feature_dim = config.featureDim;

    if (config.type == Type::SenseVoice) {
        const QString model = modelPath(config.modelDir, config.senseVoiceModelFile);
        if (!fileExists(model, errorMessage) || !fileExists(tokens, errorMessage))
            return false;
        m_offline.senseVoiceModel = model.toUtf8().toStdString();
        m_offline.tokensPath = tokens.toUtf8().toStdString();
        recognizerConfig.model_config.sense_voice.model = m_offline.senseVoiceModel.c_str();
        recognizerConfig.model_config.sense_voice.language = config.senseVoiceLanguage.toUtf8().constData();
        recognizerConfig.model_config.sense_voice.use_itn = config.senseVoiceUseItn ? 1 : 0;
    } else if (config.type == Type::FunASRNano) {
        const QString adaptor = modelPath(config.modelDir, config.funasrEncoderAdaptorFile);
        const QString llm = modelPath(config.modelDir, config.funasrLlmFile);
        const QString embed = modelPath(config.modelDir, config.funasrEmbeddingFile);
        const QString tok = modelPath(config.modelDir, config.funasrTokenizerFile);
        if (!fileExists(adaptor, errorMessage) ||
            !fileExists(llm, errorMessage) ||
            !fileExists(embed, errorMessage))
            return false;
        m_offline.funasrAdaptor = adaptor.toUtf8().toStdString();
        m_offline.funasrLlm = llm.toUtf8().toStdString();
        m_offline.funasrEmbed = embed.toUtf8().toStdString();
        recognizerConfig.model_config.funasr_nano.encoder_adaptor = m_offline.funasrAdaptor.c_str();
        recognizerConfig.model_config.funasr_nano.llm = m_offline.funasrLlm.c_str();
        recognizerConfig.model_config.funasr_nano.embedding = m_offline.funasrEmbed.c_str();
        m_offline.funasrTok = tok.toUtf8().toStdString();
        recognizerConfig.model_config.funasr_nano.tokenizer = m_offline.funasrTok.c_str();
        m_offline.funasrSysPrompt = config.funasrSystemPrompt.toUtf8().toStdString();
        m_offline.funasrUserPrompt = config.funasrUserPrompt.toUtf8().toStdString();
        recognizerConfig.model_config.funasr_nano.system_prompt = m_offline.funasrSysPrompt.c_str();
        recognizerConfig.model_config.funasr_nano.user_prompt = m_offline.funasrUserPrompt.c_str();
        recognizerConfig.model_config.funasr_nano.max_new_tokens = config.funasrMaxNewTokens;
        recognizerConfig.model_config.funasr_nano.temperature = config.funasrTemperature;
        recognizerConfig.model_config.funasr_nano.top_p = config.funasrTopP;
        recognizerConfig.model_config.funasr_nano.seed = config.funasrSeed;
    } else if (config.type == Type::Qwen3ASR) {
        const QString frontend = modelPath(config.modelDir, config.qwen3ConvFrontendFile);
        const QString enc = modelPath(config.modelDir, config.qwen3EncoderFile);
        const QString dec = modelPath(config.modelDir, config.qwen3DecoderFile);
        if (!fileExists(frontend, errorMessage) ||
            !fileExists(enc, errorMessage) ||
            !fileExists(dec, errorMessage))
            return false;
        m_offline.qwen3Frontend = frontend.toUtf8().toStdString();
        m_offline.qwen3Encoder = enc.toUtf8().toStdString();
        m_offline.qwen3Decoder = dec.toUtf8().toStdString();
        recognizerConfig.model_config.qwen3_asr.conv_frontend = m_offline.qwen3Frontend.c_str();
        recognizerConfig.model_config.qwen3_asr.encoder = m_offline.qwen3Encoder.c_str();
        recognizerConfig.model_config.qwen3_asr.decoder = m_offline.qwen3Decoder.c_str();
        m_offline.qwen3TokDir = config.qwen3TokenizerDir.toUtf8().toStdString();
        recognizerConfig.model_config.qwen3_asr.tokenizer = m_offline.qwen3TokDir.c_str();
        recognizerConfig.model_config.qwen3_asr.max_total_len = config.qwen3MaxTotalLen;
        recognizerConfig.model_config.qwen3_asr.max_new_tokens = config.qwen3MaxNewTokens;
        recognizerConfig.model_config.qwen3_asr.temperature = config.qwen3Temperature;
        recognizerConfig.model_config.qwen3_asr.top_p = config.qwen3TopP;
        recognizerConfig.model_config.qwen3_asr.seed = config.qwen3Seed;
    }

    if (!tokens.isEmpty() && QFileInfo::exists(tokens)) {
        m_offline.tokensPath = tokens.toUtf8().toStdString();
        recognizerConfig.model_config.tokens = m_offline.tokensPath.c_str();
    }

    recognizerConfig.model_config.provider = "cpu";
    recognizerConfig.model_config.num_threads = std::max(1, config.numThreads);
    recognizerConfig.model_config.modeling_unit = "cjkchar";
    recognizerConfig.decoding_method = "greedy_search";
    recognizerConfig.max_active_paths = 4;

    m_offline.recognizer = SherpaOnnxCreateOfflineRecognizer(&recognizerConfig);
    if (!m_offline.recognizer) {
        stop();
        if (errorMessage)
            *errorMessage = QStringLiteral("Failed to create offline recognizer.");
        return false;
    }

    m_offline.sampleRate = config.sampleRate;
    m_offline.samples.clear();
    return true;
}

void SpeechRecognizer::stop()
{
    if (m_punct) {
        SherpaOnnxDestroyOfflinePunctuation(m_punct);
        m_punct = nullptr;
    }
    if (m_online.recognizer) {
        if (m_online.stream) {
            SherpaOnnxDestroyOnlineStream(m_online.stream);
            m_online.stream = nullptr;
        }
        SherpaOnnxDestroyOnlineRecognizer(m_online.recognizer);
        m_online.recognizer = nullptr;
        m_online.lastText.clear();
        m_online.encoderPath.clear();
        m_online.decoderPath.clear();
        m_online.joinerPath.clear();
        m_online.tokensPath.clear();
    }
    if (m_offline.recognizer) {
        SherpaOnnxDestroyOfflineRecognizer(m_offline.recognizer);
        m_offline.recognizer = nullptr;
        m_offline.samples.clear();
        m_offline.senseVoiceModel.clear();
        m_offline.funasrAdaptor.clear();
        m_offline.funasrLlm.clear();
        m_offline.funasrEmbed.clear();
        m_offline.funasrTok.clear();
        m_offline.funasrSysPrompt.clear();
        m_offline.funasrUserPrompt.clear();
        m_offline.qwen3Frontend.clear();
        m_offline.qwen3Encoder.clear();
        m_offline.qwen3Decoder.clear();
        m_offline.qwen3TokDir.clear();
        m_offline.tokensPath.clear();
    }
}

bool SpeechRecognizer::isRunning() const
{
    return m_online.recognizer || m_offline.recognizer;
}

void SpeechRecognizer::acceptPcm16(const QByteArray &audioData,
                                   int sampleRate, int channelCount)
{
    if (audioData.isEmpty() || sampleRate <= 0 || channelCount <= 0)
        return;

    if (m_online.recognizer)
        acceptOnlinePcm16(audioData, sampleRate, channelCount);
    else if (m_offline.recognizer)
        acceptOfflinePcm16(audioData, sampleRate, channelCount);
}

void SpeechRecognizer::acceptOnlinePcm16(const QByteArray &audioData,
                                         int sampleRate, int channelCount)
{
    constexpr int bytesPerSample = 2;
    const int bytesPerFrame = bytesPerSample * channelCount;
    const int frameCount = audioData.size() / bytesPerFrame;
    if (frameCount <= 0) return;

    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(frameCount));

    const auto *data = reinterpret_cast<const uchar *>(audioData.constData());
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
        m_online.stream, sampleRate, samples.data(),
        static_cast<int32_t>(samples.size()));
    decodePendingOnline();
}

void SpeechRecognizer::acceptOfflinePcm16(const QByteArray &audioData,
                                          int sampleRate, int channelCount)
{
    constexpr int bytesPerSample = 2;
    const int bytesPerFrame = bytesPerSample * channelCount;
    const int frameCount = audioData.size() / bytesPerFrame;
    if (frameCount <= 0) return;

    m_offline.samples.reserve(m_offline.samples.size() +
                              static_cast<size_t>(frameCount));

    const auto *data = reinterpret_cast<const uchar *>(audioData.constData());
    for (int frame = 0; frame < frameCount; ++frame) {
        float mixed = 0.0F;
        const int frameOffset = frame * bytesPerFrame;
        for (int channel = 0; channel < channelCount; ++channel) {
            const int sampleOffset = frameOffset + channel * bytesPerSample;
            const qint16 sample =
                qFromLittleEndian<qint16>(data + sampleOffset);
            mixed += static_cast<float>(sample) / 32768.0F;
        }
        m_offline.samples.push_back(mixed / static_cast<float>(channelCount));
    }
}

void SpeechRecognizer::finish()
{
    if (m_online.recognizer) {
        SherpaOnnxOnlineStreamInputFinished(m_online.stream);
        decodePendingOnline();
        publishOnlineResult(true);
    } else if (m_offline.recognizer) {
        decodeOffline();
    }
}

void SpeechRecognizer::resetStream()
{
    if (m_online.recognizer && m_online.stream) {
        SherpaOnnxDestroyOnlineStream(m_online.stream);
        m_online.stream = SherpaOnnxCreateOnlineStream(m_online.recognizer);
        m_online.lastText.clear();
    }
    if (m_offline.recognizer) {
        m_offline.samples.clear();
    }
}

// ── Online helpers ────────────────────────────────────────────

void SpeechRecognizer::decodePendingOnline()
{
    while (SherpaOnnxIsOnlineStreamReady(m_online.recognizer, m_online.stream)) {
        SherpaOnnxDecodeOnlineStream(m_online.recognizer, m_online.stream);
    }

    publishOnlineResult(false);

    if (SherpaOnnxOnlineStreamIsEndpoint(m_online.recognizer, m_online.stream)) {
        publishOnlineResult(true);
        SherpaOnnxOnlineStreamReset(m_online.recognizer, m_online.stream);
        m_online.lastText.clear();
    }
}

void SpeechRecognizer::publishOnlineResult(bool isFinal)
{
    const SherpaOnnxOnlineRecognizerResult *result =
        SherpaOnnxGetOnlineStreamResult(m_online.recognizer, m_online.stream);
    if (!result) return;

    QString text = decodeSherpaText(result->text);
    SherpaOnnxDestroyOnlineRecognizerResult(result);

    if (text.isEmpty()) return;

    if (isFinal && m_punct)
        text = addPunctuation(text);

    if (!isFinal && text == m_online.lastText) return;

    m_online.lastText = text;
    emit resultChanged(text, isFinal);
}

QString SpeechRecognizer::addPunctuation(const QString &text)
{
    if (text.isEmpty() || !m_punct)
        return text;

    const std::string utf8Text = text.toUtf8().toStdString();
    const char *punctResult = SherpaOfflinePunctuationAddPunct(m_punct, utf8Text.c_str());
    if (!punctResult)
        return text;

    QString result = QString::fromUtf8(punctResult).trimmed();
    SherpaOfflinePunctuationFreeText(punctResult);
    return result.isEmpty() ? text : result;
}

// ── Offline helper ────────────────────────────────────────────

void SpeechRecognizer::decodeOffline()
{
    if (m_offline.samples.empty()) return;

    const SherpaOnnxOfflineStream *stream =
        SherpaOnnxCreateOfflineStream(m_offline.recognizer);
    if (!stream) {
        emit logMessage(QStringLiteral("Failed to create offline stream."));
        return;
    }

    SherpaOnnxAcceptWaveformOffline(
        stream, m_offline.sampleRate, m_offline.samples.data(),
        static_cast<int32_t>(m_offline.samples.size()));
    SherpaOnnxDecodeOfflineStream(m_offline.recognizer, stream);

    const SherpaOnnxOfflineRecognizerResult *result =
        SherpaOnnxGetOfflineStreamResult(stream);
    if (result) {
        QString text = decodeSherpaText(result->text);
        if (m_punct)
            text = addPunctuation(text);
        if (!text.isEmpty())
            emit resultChanged(text, true);
        SherpaOnnxDestroyOfflineRecognizerResult(result);
    }

    SherpaOnnxDestroyOfflineStream(stream);
    m_offline.samples.clear();
}

} // namespace talkinput
