#include <sherpa-onnx/c-api/c-api.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QtEndian>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{

constexpr int sampleRate = 16000;
constexpr int chunkSeconds = 20;
constexpr int chunkSamples = sampleRate * chunkSeconds;

QString defaultModelDir()
{
    return QDir::current().filePath(
        "Models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17");
}

QString defaultAudioPath()
{
    return QStringLiteral(
        "C:/Users/xiaoz/Music/meetily-recordings/audio.mp4");
}

QString modelFile(const QString &modelDir, const QString &name)
{
    return QDir(modelDir).filePath(name);
}

bool requireFile(const QString &path)
{
    if (QFileInfo::exists(path) && QFileInfo(path).isFile()) {
        return true;
    }

    qCritical() << "Missing file:" << QDir::toNativeSeparators(path);
    return false;
}

QByteArray decodeAudio(const QString &audioPath)
{
    QProcess ffmpeg;
    ffmpeg.setProgram("ffmpeg");
    ffmpeg.setArguments({
        "-v", "error", "-i", audioPath, "-f", "s16le", "-acodec",
        "pcm_s16le", "-ac", "1", "-ar", QString::number(sampleRate), "-"
    });
    ffmpeg.setProcessChannelMode(QProcess::SeparateChannels);
    ffmpeg.start();

    if (!ffmpeg.waitForStarted()) {
        qCritical() << "Failed to start ffmpeg:" << ffmpeg.errorString();
        return {};
    }

    QByteArray pcm;
    while (ffmpeg.state() != QProcess::NotRunning) {
        ffmpeg.waitForReadyRead(100);
        pcm.append(ffmpeg.readAllStandardOutput());
    }
    pcm.append(ffmpeg.readAllStandardOutput());

    const QString errorText = QString::fromUtf8(ffmpeg.readAllStandardError());
    if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
        qCritical() << "ffmpeg failed:" << errorText.trimmed();
        return {};
    }

    return pcm.left((pcm.size() / 2) * 2);
}

std::vector<float> pcm16ToFloat(const QByteArray &pcm)
{
    const int sampleCount = pcm.size() / 2;
    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(sampleCount));

    const auto *data = reinterpret_cast<const uchar *>(pcm.constData());
    for (int i = 0; i < sampleCount; ++i) {
        const qint16 sample = qFromLittleEndian<qint16>(data + i * 2);
        samples.push_back(static_cast<float>(sample) / 32768.0F);
    }

    return samples;
}

QString decodeChunk(const SherpaOnnxOfflineRecognizer *recognizer,
                    const float *samples, int sampleCount, int chunkIndex)
{
    const SherpaOnnxOfflineStream *stream =
        SherpaOnnxCreateOfflineStream(recognizer);
    if (!stream) {
        qCritical() << "Failed to create offline stream.";
        return {};
    }

    SherpaOnnxAcceptWaveformOffline(stream, sampleRate, samples, sampleCount);
    SherpaOnnxDecodeOfflineStream(recognizer, stream);

    const SherpaOnnxOfflineRecognizerResult *result =
        SherpaOnnxGetOfflineStreamResult(stream);
    QString text;
    if (result) {
        text = QString::fromUtf8(result->text ? result->text : "").trimmed();
        const QString json =
            QString::fromUtf8(result->json ? result->json : "").trimmed();
        if (!json.isEmpty()) {
            qInfo() << QStringLiteral("[chunk %1 json] %2")
                                     .arg(chunkIndex)
                                     .arg(json);
        }
        SherpaOnnxDestroyOfflineRecognizerResult(result);
    }

    SherpaOnnxDestroyOfflineStream(stream);
    return text;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();

    const QString modelDir =
        args.size() > 1 ? QDir::fromNativeSeparators(args.at(1))
                        : defaultModelDir();
    const QString audioPath =
        args.size() > 2 ? QDir::fromNativeSeparators(args.at(2))
                        : defaultAudioPath();

    const QString model = modelFile(modelDir, "model.int8.onnx");
    const QString tokens = modelFile(modelDir, "tokens.txt");
    if (!requireFile(model) || !requireFile(tokens) || !requireFile(audioPath)) {
        return 2;
    }

    qInfo() << "SenseVoice model:" << QDir::toNativeSeparators(modelDir);
    qInfo() << "Audio:" << QDir::toNativeSeparators(audioPath);

    const QByteArray modelUtf8 = model.toUtf8();
    const QByteArray tokensUtf8 = tokens.toUtf8();

    SherpaOnnxOfflineRecognizerConfig config;
    std::memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = sampleRate;
    config.feat_config.feature_dim = 80;
    config.model_config.sense_voice.model = modelUtf8.constData();
    config.model_config.sense_voice.language = "zh";
    config.model_config.sense_voice.use_itn = 1;
    config.model_config.tokens = tokensUtf8.constData();
    config.model_config.num_threads = 2;
    config.model_config.provider = "cpu";
    config.model_config.modeling_unit = "cjkchar";
    config.decoding_method = "greedy_search";
    config.max_active_paths = 4;

    const SherpaOnnxOfflineRecognizer *recognizer =
        SherpaOnnxCreateOfflineRecognizer(&config);
    if (!recognizer) {
        qCritical() << "Failed to create SenseVoice recognizer.";
        return 3;
    }

    const QByteArray pcm = decodeAudio(audioPath);
    if (pcm.isEmpty()) {
        SherpaOnnxDestroyOfflineRecognizer(recognizer);
        return 4;
    }

    const std::vector<float> samples = pcm16ToFloat(pcm);
    qInfo() << QStringLiteral("Decoded audio: %1 seconds")
                             .arg(static_cast<double>(samples.size()) /
                                  sampleRate, 0, 'f', 2);

    QStringList transcript;
    int chunkIndex = 0;
    for (size_t offset = 0; offset < samples.size();
         offset += static_cast<size_t>(chunkSamples)) {
        const int count = static_cast<int>(
            std::min(static_cast<size_t>(chunkSamples),
                     samples.size() - offset));
        if (count <= 0) {
            continue;
        }

        const QString text =
            decodeChunk(recognizer, samples.data() + offset, count, chunkIndex);
        if (!text.isEmpty()) {
            transcript.append(text);
            qInfo() << QStringLiteral("[chunk %1 text] %2")
                                     .arg(chunkIndex)
                                     .arg(text);
        }
        ++chunkIndex;
    }

    qInfo() << "";
    qInfo() << "==== SenseVoice Transcript ====";
    qInfo() << transcript.join("");

    SherpaOnnxDestroyOfflineRecognizer(recognizer);
    return 0;
}
