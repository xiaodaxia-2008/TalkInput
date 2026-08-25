#include <sherpa-onnx/c-api/c-api.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QtEndian>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

namespace
{

constexpr int sampleRate = 16000;

struct Options
{
    QString modelDir;
    QString audioPath;
    int numThreads = 2;
    int parallelism = 2;
    int chunkCount = 4;
    int repeats = 2;
};

class StringPool
{
public:
    const char *store(const QString &value)
    {
        m_values.push_back(value.toStdString());
        return m_values.back().c_str();
    }

private:
    std::deque<std::string> m_values;
};

struct AudioChunk
{
    std::vector<float> samples;
};

using OfflineStream = const SherpaOnnxOfflineStream *;

void printUsage()
{
    std::cout
        << "Usage: ZennyFunAsrParallelBenchmark --dir <modelDir> "
           "--audio <audioFile> [options]\n"
        << "  --threads <n>       ONNX Runtime threads per stream (default: "
           "2)\n"
        << "  --parallel <n>      streams decoded per batch (default: 2)\n"
        << "  --chunks <n>        number of independent audio chunks (default: "
           "4)\n"
        << "  --repeats <n>       timed repetitions (default: 2)\n";
}

Options parseOptions(const QStringList &args)
{
    Options options;
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args[i];
        auto readInt = [&](int *target) {
            if (i + 1 >= args.size()) {
                std::cerr << "Missing value for " << arg.toStdString()
                          << std::endl;
                std::exit(1);
            }
            bool ok = false;
            const int value = args[++i].toInt(&ok);
            if (!ok || value <= 0) {
                std::cerr << "Invalid value for " << arg.toStdString()
                          << std::endl;
                std::exit(1);
            }
            *target = value;
        };

        if (arg == "--dir" && i + 1 < args.size()) {
            options.modelDir = QDir::fromNativeSeparators(args[++i]);
        }
        else if (arg == "--audio" && i + 1 < args.size()) {
            options.audioPath = QDir::fromNativeSeparators(args[++i]);
        }
        else if (arg == "--threads") {
            readInt(&options.numThreads);
        }
        else if (arg == "--parallel") {
            readInt(&options.parallelism);
        }
        else if (arg == "--chunks") {
            readInt(&options.chunkCount);
        }
        else if (arg == "--repeats") {
            readInt(&options.repeats);
        }
        else if (arg == "--help") {
            printUsage();
            std::exit(0);
        }
        else {
            std::cerr << "Unknown or incomplete argument: " << arg.toStdString()
                      << std::endl;
            printUsage();
            std::exit(1);
        }
    }

    if (options.modelDir.isEmpty() || options.audioPath.isEmpty()) {
        std::cerr << "Both --dir and --audio are required" << std::endl;
        printUsage();
        std::exit(1);
    }
    return options;
}

bool requireFile(const QString &path)
{
    const QFileInfo info(path);
    if (info.exists() && (info.isFile() || info.isDir())) {
        return true;
    }
    std::cerr << "Missing file: "
              << QDir::toNativeSeparators(path).toStdString() << std::endl;
    return false;
}

QByteArray decodeAudioToPcm(const QString &audioPath)
{
    QProcess ffmpeg;
    ffmpeg.setProgram("ffmpeg");
    ffmpeg.setArguments({"-v", "error", "-i", audioPath, "-f", "s16le",
                         "-acodec", "pcm_s16le", "-ac", "1", "-ar",
                         QString::number(sampleRate), "-"});
    ffmpeg.setProcessChannelMode(QProcess::SeparateChannels);
    ffmpeg.start();
    if (!ffmpeg.waitForStarted()) {
        std::cerr << "ffmpeg start failed: "
                  << ffmpeg.errorString().toStdString() << std::endl;
        return {};
    }

    QByteArray pcm;
    while (ffmpeg.state() != QProcess::NotRunning) {
        ffmpeg.waitForReadyRead(100);
        pcm.append(ffmpeg.readAllStandardOutput());
    }
    pcm.append(ffmpeg.readAllStandardOutput());

    const QString error = QString::fromUtf8(ffmpeg.readAllStandardError());
    if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
        std::cerr << "ffmpeg error: " << error.trimmed().toStdString()
                  << std::endl;
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
        const qint16 value = qFromLittleEndian<qint16>(data + i * 2);
        samples.push_back(static_cast<float>(value) / 32768.0F);
    }
    return samples;
}

std::vector<AudioChunk> splitAudio(const std::vector<float> &samples,
                                   int chunkCount)
{
    const int actualCount = std::min(
        chunkCount, static_cast<int>(std::max<size_t>(1, samples.size())));
    std::vector<AudioChunk> chunks;
    chunks.reserve(static_cast<size_t>(actualCount));

    for (int i = 0; i < actualCount; ++i) {
        const size_t begin = samples.size() * static_cast<size_t>(i) /
                             static_cast<size_t>(actualCount);
        const size_t end = samples.size() * static_cast<size_t>(i + 1) /
                           static_cast<size_t>(actualCount);
        if (end > begin) {
            chunks.push_back({std::vector<float>(samples.begin() + begin,
                                                 samples.begin() + end)});
        }
    }
    return chunks;
}

const SherpaOnnxOfflineRecognizer *createRecognizer(const Options &options,
                                                    StringPool &pool)
{
    SherpaOnnxOfflineRecognizerConfig config;
    std::memset(&config, 0, sizeof(config));
    config.feat_config.sample_rate = sampleRate;
    config.feat_config.feature_dim = 80;

    config.model_config.funasr_nano.encoder_adaptor = pool.store(
        QDir(options.modelDir).filePath("encoder_adaptor.int8.onnx"));
    config.model_config.funasr_nano.llm =
        pool.store(QDir(options.modelDir).filePath("llm.int8.onnx"));
    config.model_config.funasr_nano.embedding =
        pool.store(QDir(options.modelDir).filePath("embedding.int8.onnx"));
    config.model_config.funasr_nano.tokenizer =
        pool.store(QDir(options.modelDir).filePath("Qwen3-0.6B"));
    config.model_config.funasr_nano.system_prompt =
        "You are a helpful assistant.";
    config.model_config.funasr_nano.user_prompt = "语音转写：";
    config.model_config.funasr_nano.max_new_tokens = 128;
    config.model_config.funasr_nano.temperature = 1e-6F;
    config.model_config.funasr_nano.top_p = 0.8F;
    config.model_config.funasr_nano.seed = 42;
    config.model_config.funasr_nano.language = "";
    config.model_config.funasr_nano.itn = 1;
    config.model_config.provider = "cpu";
    config.model_config.num_threads = options.numThreads;
    config.model_config.modeling_unit = "cjkchar";
    config.decoding_method = "greedy_search";
    config.max_active_paths = 4;

    return SherpaOnnxCreateOfflineRecognizer(&config);
}

OfflineStream createStream(const AudioChunk &chunk,
                           const SherpaOnnxOfflineRecognizer *recognizer)
{
    const OfflineStream stream = SherpaOnnxCreateOfflineStream(recognizer);
    if (!stream) {
        return nullptr;
    }
    SherpaOnnxAcceptWaveformOffline(stream, sampleRate, chunk.samples.data(),
                                    static_cast<int32_t>(chunk.samples.size()));
    return stream;
}

QString streamText(OfflineStream stream)
{
    const SherpaOnnxOfflineRecognizerResult *result =
        SherpaOnnxGetOfflineStreamResult(stream);
    if (!result) {
        return {};
    }
    const QString text =
        QString::fromUtf8(result->text ? result->text : "").trimmed();
    SherpaOnnxDestroyOfflineRecognizerResult(result);
    return text;
}

QStringList decodeSerial(const SherpaOnnxOfflineRecognizer *recognizer,
                         const std::vector<AudioChunk> &chunks)
{
    QStringList result;
    for (const auto &chunk : chunks) {
        const OfflineStream stream = createStream(chunk, recognizer);
        if (!stream) {
            return {};
        }
        SherpaOnnxDecodeOfflineStream(recognizer, stream);
        result.append(streamText(stream));
        SherpaOnnxDestroyOfflineStream(stream);
    }
    return result;
}

QStringList decodeParallel(const SherpaOnnxOfflineRecognizer *recognizer,
                           const std::vector<AudioChunk> &chunks,
                           int parallelism)
{
    QStringList result;
    result.reserve(static_cast<int>(chunks.size()));

    for (size_t offset = 0; offset < chunks.size();
         offset += static_cast<size_t>(parallelism))
    {
        const size_t count =
            std::min(static_cast<size_t>(parallelism), chunks.size() - offset);
        std::vector<OfflineStream> streams;
        streams.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            const OfflineStream stream =
                createStream(chunks[offset + i], recognizer);
            if (!stream) {
                for (const auto created : streams) {
                    SherpaOnnxDestroyOfflineStream(created);
                }
                return {};
            }
            streams.push_back(stream);
        }

        SherpaOnnxDecodeMultipleOfflineStreams(
            recognizer, streams.data(), static_cast<int32_t>(streams.size()));

        // The API fills all streams in parallel; collect them in input order.
        for (const auto stream : streams) {
            result.append(streamText(stream));
            SherpaOnnxDestroyOfflineStream(stream);
        }
    }
    return result;
}

double measureSerial(const SherpaOnnxOfflineRecognizer *recognizer,
                     const std::vector<AudioChunk> &chunks, QStringList *result)
{
    QElapsedTimer timer;
    timer.start();
    *result = decodeSerial(recognizer, chunks);
    return static_cast<double>(timer.nsecsElapsed()) / 1e9;
}

double measureParallel(const SherpaOnnxOfflineRecognizer *recognizer,
                       const std::vector<AudioChunk> &chunks, int parallelism,
                       QStringList *result)
{
    QElapsedTimer timer;
    timer.start();
    *result = decodeParallel(recognizer, chunks, parallelism);
    return static_cast<double>(timer.nsecsElapsed()) / 1e9;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const Options options = parseOptions(app.arguments());

    const QStringList modelFiles = {"encoder_adaptor.int8.onnx",
                                    "llm.int8.onnx", "embedding.int8.onnx",
                                    "Qwen3-0.6B"};
    for (const auto &file : modelFiles) {
        if (!requireFile(QDir(options.modelDir).filePath(file))) {
            return 1;
        }
    }
    if (!requireFile(options.audioPath)) {
        return 1;
    }

    const QByteArray pcm = decodeAudioToPcm(options.audioPath);
    const std::vector<float> samples = pcm16ToFloat(pcm);
    const std::vector<AudioChunk> chunks =
        splitAudio(samples, options.chunkCount);
    if (chunks.size() < 2) {
        std::cerr << "Audio must produce at least two non-empty chunks"
                  << std::endl;
        return 1;
    }

    StringPool pool;
    const SherpaOnnxOfflineRecognizer *recognizer =
        createRecognizer(options, pool);
    if (!recognizer) {
        std::cerr << "Failed to create FunASR Nano recognizer" << std::endl;
        return 1;
    }

    // Warm up both code paths before collecting timed samples.
    QStringList ignored;
    decodeSerial(recognizer, chunks);
    decodeParallel(recognizer, chunks, options.parallelism);

    double serialTotal = 0.0;
    double parallelTotal = 0.0;
    QStringList serialResult;
    QStringList parallelResult;
    for (int i = 0; i < options.repeats; ++i) {
        serialTotal += measureSerial(recognizer, chunks, &serialResult);
        parallelTotal += measureParallel(recognizer, chunks,
                                         options.parallelism, &parallelResult);
    }

    const QString serialText = serialResult.join("");
    const QString parallelText = parallelResult.join("");
    const bool sameResult = serialResult == parallelResult;
    const double serialAverage = serialTotal / options.repeats;
    const double parallelAverage = parallelTotal / options.repeats;

    std::cout << "FunASR Nano multi-stream benchmark\n"
              << "  model dir       : "
              << QDir::toNativeSeparators(options.modelDir).toStdString()
              << "\n"
              << "  audio           : "
              << QDir::toNativeSeparators(options.audioPath).toStdString()
              << "\n"
              << "  ORT threads     : " << options.numThreads << "\n"
              << "  chunks          : " << chunks.size() << "\n"
              << "  parallel streams: " << options.parallelism << "\n"
              << "  serial average  : " << serialAverage << " s\n"
              << "  parallel average: " << parallelAverage << " s\n"
              << "  speedup         : " << serialAverage / parallelAverage
              << "x\n"
              << "  same chunk text : " << (sameResult ? "yes" : "NO") << "\n"
              << "  serial text     : " << serialText.toStdString() << "\n"
              << "  parallel text   : " << parallelText.toStdString()
              << std::endl;

    SherpaOnnxDestroyOfflineRecognizer(recognizer);
    return sameResult ? 0 : 2;
}
