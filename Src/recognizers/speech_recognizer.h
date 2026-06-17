#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>
#include <string>
#include <vector>

struct SherpaOnnxOfflinePunctuation;

namespace talkinput
{

class SpeechRecognizer : public QObject
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
        QString funasrLanguage;
        bool funasrItn = true;

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

    virtual bool start(const Config &config, QString *errorMessage) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual bool isStreaming() const = 0;

    virtual void acceptPcm16(const QByteArray &audioData, int sampleRate,
                             int channelCount) = 0;
    virtual void finish() = 0;
    virtual void resetStream() = 0;

signals:
    void logMessage(const QString &message);
    void resultChanged(const QString &text, bool isFinal);

protected:
    bool prepareRecognizer(const Config &config, QString *errorMessage);
    void stopPunctuation();
    QString addPunctuation(const QString &text) const;

    static QString modelPath(const QString &modelDir, const QString &fileName);
    static bool fileExists(const QString &path, QString *errorMessage);
    static bool pathExists(const QString &path, QString *errorMessage);
    static QString decodeSherpaText(const char *text);
    static int appendPcm16AsMonoFloat(const QByteArray &audioData,
                                      int channelCount,
                                      std::vector<float> *samples);

private:
    const SherpaOnnxOfflinePunctuation *m_punct = nullptr;
};

std::unique_ptr<SpeechRecognizer>
createSpeechRecognizer(SpeechRecognizer::Type type, QObject *parent = nullptr);

} // namespace talkinput
