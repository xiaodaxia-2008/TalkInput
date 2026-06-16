#include <sherpa-onnx/c-api/c-api.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
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

QString defaultModelDir()
{
    return QDir::current().filePath(
        "Models/sherpa-onnx-streaming-zipformer-zh-xlarge-int8-2025-06-30");
}

QString defaultAudioPath()
{
    return QStringLiteral(
        "C:/Users/xiaoz/Music/meetily-recordings/audio.mp4");
}

QString defaultHotwordsPath()
{
    return QDir::current().filePath("hot_words.txt");
}

bool requireFile(const QString &path)
{
    if (QFileInfo::exists(path) && QFileInfo(path).isFile()) {
        return true;
    }

    qCritical().noquote() << "Missing file:" << QDir::toNativeSeparators(path);
    return false;
}

QString modelFile(const QString &modelDir, const QString &name)
{
    return QDir(modelDir).filePath(name);
}

QString formatCjkHotwordLine(const QString &line)
{
    QStringList tokens;
    const QString trimmed = line.trimmed();
    tokens.reserve(trimmed.size());

    for (const QChar ch : trimmed) {
        if (!ch.isSpace()) {
            tokens.append(QString(ch));
        }
    }

    return tokens.join(QLatin1Char(' '));
}

QString buildHotwordsText(const QString &input)
{
    QStringList hotwords;
    const QStringList lines = input.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line.remove(QLatin1Char('\r'));
        const QString hotword = formatCjkHotwordLine(line);
        if (!hotword.isEmpty()) {
            hotwords.append(hotword);
        }
    }

    return hotwords.join(QLatin1Char('\n'));
}

QString readHotwordsFile(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open hotwords file: %1")
                                .arg(QDir::toNativeSeparators(path));
        }
        return {};
    }

    return buildHotwordsText(QString::fromUtf8(file.readAll()));
}

QString decodeSherpaText(const char *text)
{
    return QString::fromUtf8(text ? text : "").trimmed();
}

void decodeReady(const SherpaOnnxOnlineRecognizer *recognizer,
                 const SherpaOnnxOnlineStream *stream)
{
    while (SherpaOnnxIsOnlineStreamReady(recognizer, stream)) {
        SherpaOnnxDecodeOnlineStream(recognizer, stream);
    }
}

QString currentText(const SherpaOnnxOnlineRecognizer *recognizer,
                    const SherpaOnnxOnlineStream *stream)
{
    const SherpaOnnxOnlineRecognizerResult *result =
        SherpaOnnxGetOnlineStreamResult(recognizer, stream);
    if (!result) {
        return {};
    }

    const QString text = decodeSherpaText(result->text);
    SherpaOnnxDestroyOnlineRecognizerResult(result);
    return text;
}

void feedPcm16(const QByteArray &pcm,
               const SherpaOnnxOnlineRecognizer *recognizer,
               const SherpaOnnxOnlineStream *stream, QStringList *segments,
               QString *lastPartial)
{
    const int sampleCount = pcm.size() / 2;
    if (sampleCount <= 0) {
        return;
    }

    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(sampleCount));

    const auto *data = reinterpret_cast<const uchar *>(pcm.constData());
    for (int i = 0; i < sampleCount; ++i) {
        const qint16 sample = qFromLittleEndian<qint16>(data + i * 2);
        samples.push_back(static_cast<float>(sample) / 32768.0F);
    }

    SherpaOnnxOnlineStreamAcceptWaveform(
        stream, sampleRate, samples.data(),
        static_cast<int32_t>(samples.size()));
    decodeReady(recognizer, stream);

    const QString partial = currentText(recognizer, stream);
    if (!partial.isEmpty() && partial != *lastPartial) {
        qInfo().noquote() << "[partial]" << partial;
        *lastPartial = partial;
    }

    if (SherpaOnnxOnlineStreamIsEndpoint(recognizer, stream)) {
        const QString finalText = currentText(recognizer, stream);
        if (!finalText.isEmpty()) {
            segments->append(finalText);
            qInfo().noquote() << "[final]" << finalText;
        }
        SherpaOnnxOnlineStreamReset(recognizer, stream);
        lastPartial->clear();
    }
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
    const QString hotwordsPath =
        args.size() > 3 ? QDir::fromNativeSeparators(args.at(3))
                        : defaultHotwordsPath();
    const bool useHotwords = QFileInfo::exists(hotwordsPath);

    const QString encoder = modelFile(modelDir, "encoder.int8.onnx");
    const QString decoder = modelFile(modelDir, "decoder.onnx");
    const QString joiner = modelFile(modelDir, "joiner.int8.onnx");
    const QString tokens = modelFile(modelDir, "tokens.txt");

    if (!requireFile(encoder) || !requireFile(decoder) ||
        !requireFile(joiner) || !requireFile(tokens) ||
        !requireFile(audioPath)) {
        return 2;
    }

    if (useHotwords && !requireFile(hotwordsPath)) {
        return 2;
    }

    qInfo().noquote() << "Model:" << QDir::toNativeSeparators(modelDir);
    qInfo().noquote() << "Audio:" << QDir::toNativeSeparators(audioPath);
    if (useHotwords) {
        qInfo().noquote() << "Hotwords:"
                          << QDir::toNativeSeparators(hotwordsPath);
    }

    const QByteArray encoderUtf8 = encoder.toUtf8();
    const QByteArray decoderUtf8 = decoder.toUtf8();
    const QByteArray joinerUtf8 = joiner.toUtf8();
    const QByteArray tokensUtf8 = tokens.toUtf8();
    QString hotwordsText;
    QByteArray hotwordsUtf8;
    if (useHotwords) {
        QString hotwordsError;
        hotwordsText = readHotwordsFile(hotwordsPath, &hotwordsError);
        if (hotwordsText.isEmpty()) {
            qCritical().noquote() << hotwordsError;
            return 2;
        }
        hotwordsUtf8 = hotwordsText.toUtf8();
        qInfo().noquote() << "Formatted hotwords:" << hotwordsText;
    }

    SherpaOnnxOnlineRecognizerConfig config;
    std::memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = sampleRate;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder = encoderUtf8.constData();
    config.model_config.transducer.decoder = decoderUtf8.constData();
    config.model_config.transducer.joiner = joinerUtf8.constData();
    config.model_config.tokens = tokensUtf8.constData();
    config.model_config.provider = "cpu";
    config.model_config.modeling_unit = "cjkchar";
    config.model_config.num_threads = 2;
    if (useHotwords) {
        config.decoding_method = "modified_beam_search";
        config.hotwords_buf = hotwordsUtf8.constData();
        config.hotwords_buf_size = static_cast<int32_t>(hotwordsUtf8.size());
        config.hotwords_score = 1.5F;
    }
    else {
        config.decoding_method = "greedy_search";
    }
    config.max_active_paths = 4;
    config.enable_endpoint = 1;
    config.rule1_min_trailing_silence = 2.4F;
    config.rule2_min_trailing_silence = 1.2F;
    config.rule3_min_utterance_length = 20.0F;

    const SherpaOnnxOnlineRecognizer *recognizer =
        SherpaOnnxCreateOnlineRecognizer(&config);
    if (!recognizer) {
        qCritical() << "Failed to create sherpa-onnx recognizer.";
        return 3;
    }

    const SherpaOnnxOnlineStream *stream =
        SherpaOnnxCreateOnlineStream(recognizer);
    if (!stream) {
        SherpaOnnxDestroyOnlineRecognizer(recognizer);
        qCritical() << "Failed to create sherpa-onnx stream.";
        return 4;
    }

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
        SherpaOnnxDestroyOnlineStream(stream);
        SherpaOnnxDestroyOnlineRecognizer(recognizer);
        return 5;
    }

    QStringList segments;
    QString lastPartial;
    QByteArray pcmBuffer;

    while (ffmpeg.state() != QProcess::NotRunning) {
        ffmpeg.waitForReadyRead(100);
        pcmBuffer.append(ffmpeg.readAllStandardOutput());

        const int usableBytes = (pcmBuffer.size() / 2) * 2;
        if (usableBytes > 0) {
            const QByteArray chunk = pcmBuffer.left(usableBytes);
            pcmBuffer.remove(0, usableBytes);
            feedPcm16(chunk, recognizer, stream, &segments, &lastPartial);
        }
    }

    pcmBuffer.append(ffmpeg.readAllStandardOutput());
    if (pcmBuffer.size() >= 2) {
        feedPcm16(pcmBuffer.left((pcmBuffer.size() / 2) * 2), recognizer,
                  stream, &segments, &lastPartial);
    }

    const QString ffmpegError = QString::fromUtf8(ffmpeg.readAllStandardError());
    if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
        qCritical().noquote() << "ffmpeg failed:" << ffmpegError.trimmed();
    }

    SherpaOnnxOnlineStreamInputFinished(stream);
    decodeReady(recognizer, stream);

    const QString tail = currentText(recognizer, stream);
    if (!tail.isEmpty() && (segments.isEmpty() || segments.last() != tail)) {
        segments.append(tail);
        qInfo().noquote() << "[final]" << tail;
    }

    qInfo().noquote() << "";
    qInfo().noquote() << "==== Transcript ====";
    qInfo().noquote() << segments.join("");

    SherpaOnnxDestroyOnlineStream(stream);
    SherpaOnnxDestroyOnlineRecognizer(recognizer);

    return ffmpeg.exitCode() == 0 ? 0 : 6;
}
