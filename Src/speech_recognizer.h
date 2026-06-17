#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <string>
#include <vector>

struct SherpaOnnxOnlineRecognizer;
struct SherpaOnnxOnlineStream;
struct SherpaOnnxOfflineRecognizer;
struct SherpaOnnxOfflineStream;
struct SherpaOnnxOfflinePunctuation;

namespace talkinput
{

class SpeechRecognizer final : public QObject
{
    Q_OBJECT

public:
    enum class Type
    {
        StreamingTransducer,
        StreamingParaformer,
        SenseVoice,
        FunASRNano,
        Qwen3ASR,
    };

    struct Config
    {
        Type type = Type::StreamingTransducer;

        QString modelDir;
        int sampleRate = 16000;
        int featureDim = 80;
        int numThreads = 2;

        // Streaming (transducer / paraformer)
        QString encoderFile;
        QString decoderFile;
        QString joinerFile;
        QString modelingUnit = "cjkchar";

        // Common
        QString tokensFile = "tokens.txt";
        QString hotwordsText;
        float hotwordsScore = 1.5F;

        // SenseVoice
        QString senseVoiceModelFile;
        QString senseVoiceLanguage = "auto";
        bool senseVoiceUseItn = true;

        // FunASR Nano
        QString funasrEncoderAdaptorFile;
        QString funasrLlmFile;
        QString funasrEmbeddingFile;
        QString funasrTokenizerFile;
        QString funasrSystemPrompt = "You are a helpful assistant.";
        QString funasrUserPrompt = "\u8BED\u97F3\u8F6C\u5199\uFF1A";
        int funasrMaxNewTokens = 128;
        float funasrTemperature = 1e-6F;
        float funasrTopP = 0.8F;
        int funasrSeed = 42;

        // Punctuation restoration (offline model, e.g. ct-transformer)
        QString punctuationModelPath;

        // Qwen3-ASR
        QString qwen3ConvFrontendFile;
        QString qwen3EncoderFile;
        QString qwen3DecoderFile;
        QString qwen3TokenizerDir;
        int qwen3MaxTotalLen = 1024;
        int qwen3MaxNewTokens = 256;
        float qwen3Temperature = 1e-6F;
        float qwen3TopP = 0.8F;
        int qwen3Seed = 42;
    };

    explicit SpeechRecognizer(QObject *parent = nullptr);
    ~SpeechRecognizer() override;

    bool start(const Config &config, QString *errorMessage);
    void stop();
    bool isRunning() const;

    bool isStreaming() const { return m_online.recognizer != nullptr; }

    void acceptPcm16(const QByteArray &audioData, int sampleRate,
                     int channelCount);
    void finish();
    void resetStream();

signals:
    void logMessage(const QString &message);
    void resultChanged(const QString &text, bool isFinal);

private:
    bool startOnline(const Config &config, QString *errorMessage);
    bool startOffline(const Config &config, QString *errorMessage);

    void acceptOnlinePcm16(const QByteArray &audioData, int sampleRate,
                           int channelCount);
    void acceptOfflinePcm16(const QByteArray &audioData, int sampleRate,
                            int channelCount);

    void decodePendingOnline();
    void publishOnlineResult(bool isFinal);
    QString addPunctuation(const QString &text);
    void decodeOffline();

    struct OnlineState
    {
        const SherpaOnnxOnlineRecognizer *recognizer = nullptr;
        const SherpaOnnxOnlineStream *stream = nullptr;
        QString lastText;
        std::string encoderPath;
        std::string decoderPath;
        std::string joinerPath;
        std::string tokensPath;
    } m_online;

    struct OfflineState
    {
        const SherpaOnnxOfflineRecognizer *recognizer = nullptr;
        std::vector<float> samples;
        int sampleRate = 16000;

        std::string tokensPath;
        std::string senseVoiceModel;
        std::string funasrAdaptor;
        std::string funasrLlm;
        std::string funasrEmbed;
        std::string funasrTok;
        std::string funasrSysPrompt;
        std::string funasrUserPrompt;
        std::string qwen3Frontend;
        std::string qwen3Encoder;
        std::string qwen3Decoder;
        std::string qwen3TokDir;
    } m_offline;

    const SherpaOnnxOfflinePunctuation *m_punct = nullptr;
};

} // namespace talkinput
