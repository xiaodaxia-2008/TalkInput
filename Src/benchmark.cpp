#include <sherpa-onnx/c-api/c-api.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QTemporaryFile>
#include <QtEndian>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{

constexpr int sampleRate = 16000;

struct BenchConfig
{
    QString type;
    QString modelDir;
    QString audioPath;
    QString hotwordsPath;
    QString groundTruthPath;
};

// String pool that keeps C-string data alive until it goes out of scope.
class StringPool
{
public:
    const char *store(const QString &s)
    {
        m_pool.push_back(s.toStdString());
        return m_pool.back().c_str();
    }

    const char *store(const char *s)
    {
        m_pool.emplace_back(s);
        return m_pool.back().c_str();
    }

private:
    std::vector<std::string> m_pool;
};

void usage()
{
    std::cout << "Usage: TalkInputBenchmark --type <type> --dir <modelDir>"
                 " --audio <audioFile>"
                 " [--hotwords <hotwordsFile>]"
                 " [--ground-truth <groundTruthFile>]"
              << std::endl
              << std::endl
              << "Supported types:" << std::endl
              << "  transducer       (online streaming zipformer)" << std::endl
              << "  paraformer       (online streaming paraformer)" << std::endl
              << "  sense_voice      (offline SenseVoice)" << std::endl
              << "  fire_red_asr_ctc (offline FireRedAsr v2 CTC)" << std::endl
              << "  fire_red_asr_aed (offline FireRedAsr v2 AED)" << std::endl
              << "  moonshine_v2     (offline Moonshine v2, merged decoder)"
              << std::endl
              << "  funasr_nano      (offline FunASR Nano)" << std::endl
              << "  qwen3_asr        (offline Qwen3-ASR 0.6B)" << std::endl;
}

BenchConfig parseArgs(const QStringList &args)
{
    BenchConfig c;
    c.type = "transducer";

    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args[i];
        if (arg == "--type" && i + 1 < args.size()) {
            c.type = args[++i];
        }
        else if (arg == "--dir" && i + 1 < args.size()) {
            c.modelDir = QDir::fromNativeSeparators(args[++i]);
        }
        else if (arg == "--audio" && i + 1 < args.size()) {
            c.audioPath = QDir::fromNativeSeparators(args[++i]);
        }
        else if (arg == "--hotwords" && i + 1 < args.size()) {
            c.hotwordsPath = QDir::fromNativeSeparators(args[++i]);
        }
        else if (arg == "--ground-truth" && i + 1 < args.size()) {
            c.groundTruthPath = QDir::fromNativeSeparators(args[++i]);
        }
        else if (arg == "--help") {
            usage();
            std::exit(0);
        }
        else {
            std::cerr << "Unknown argument: " << arg.toStdString() << std::endl;
            std::exit(1);
        }
    }

    if (c.modelDir.isEmpty() || c.audioPath.isEmpty()) {
        std::cerr << "Missing required arguments: --dir and --audio"
                  << std::endl;
        std::exit(1);
    }

    return c;
}

bool requireFile(const QString &path)
{
    if (QFileInfo::exists(path) && QFileInfo(path).isFile()) {
        return true;
    }
    std::cerr << "Missing: " << QDir::toNativeSeparators(path).toStdString()
              << std::endl;
    return false;
}

QString modelFile(const QString &dir, const QString &name)
{
    return QDir(dir).filePath(name);
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

    const QString err = QString::fromUtf8(ffmpeg.readAllStandardError());
    if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
        std::cerr << "ffmpeg error: " << err.trimmed().toStdString()
                  << std::endl;
        return {};
    }

    return pcm.left((pcm.size() / 2) * 2);
}

std::vector<float> pcm16ToFloat(const QByteArray &pcm)
{
    const int n = pcm.size() / 2;
    std::vector<float> out;
    out.reserve(static_cast<size_t>(n));
    const auto *data = reinterpret_cast<const uchar *>(pcm.constData());
    for (int i = 0; i < n; ++i) {
        qint16 s = qFromLittleEndian<qint16>(data + i * 2);
        out.push_back(static_cast<float>(s) / 32768.0F);
    }
    return out;
}

struct OfflineSetup
{
    SherpaOnnxOfflineRecognizerConfig conf;
    StringPool pool;
    QTemporaryFile hwFile;
};

void setupSenseVoice(const BenchConfig &cfg, OfflineSetup &s)
{
    s.pool.store("zh"); // language hint is stored too
    s.conf.model_config.sense_voice.model =
        s.pool.store(modelFile(cfg.modelDir, "model.int8.onnx"));
    s.conf.model_config.sense_voice.language = "zh";
    s.conf.model_config.sense_voice.use_itn = 1;
}

void setupFireRedAsrCtc(const BenchConfig &cfg, OfflineSetup &s)
{
    s.conf.model_config.fire_red_asr_ctc.model =
        s.pool.store(modelFile(cfg.modelDir, "model.int8.onnx"));
}

void setupFireRedAsrAed(const BenchConfig &cfg, OfflineSetup &s)
{
    s.conf.model_config.fire_red_asr.encoder =
        s.pool.store(modelFile(cfg.modelDir, "encoder.int8.onnx"));
    s.conf.model_config.fire_red_asr.decoder =
        s.pool.store(modelFile(cfg.modelDir, "decoder.int8.onnx"));
}

void setupMoonshineV2(const BenchConfig &cfg, OfflineSetup &s)
{
    QString pre = modelFile(cfg.modelDir, "preprocessor.ort");
    if (QFileInfo::exists(pre)) {
        s.conf.model_config.moonshine.preprocessor = s.pool.store(pre);
    }
    s.conf.model_config.moonshine.encoder =
        s.pool.store(modelFile(cfg.modelDir, "encoder_model.ort"));
    s.conf.model_config.moonshine.merged_decoder =
        s.pool.store(modelFile(cfg.modelDir, "decoder_model_merged.ort"));
}

void setupFunasrNano(const BenchConfig &cfg, OfflineSetup &s)
{
    s.conf.model_config.funasr_nano.encoder_adaptor =
        s.pool.store(modelFile(cfg.modelDir, "encoder_adaptor.int8.onnx"));
    s.conf.model_config.funasr_nano.llm =
        s.pool.store(modelFile(cfg.modelDir, "llm.int8.onnx"));
    s.conf.model_config.funasr_nano.embedding =
        s.pool.store(modelFile(cfg.modelDir, "embedding.int8.onnx"));
    s.conf.model_config.funasr_nano.tokenizer =
        s.pool.store(modelFile(cfg.modelDir, "Qwen3-0.6B"));
    s.conf.model_config.funasr_nano.system_prompt =
        "You are a helpful assistant.";
    s.conf.model_config.funasr_nano.user_prompt = "语音转写：";
    s.conf.model_config.funasr_nano.max_new_tokens = 128;
    s.conf.model_config.funasr_nano.temperature = 1e-6F;
    s.conf.model_config.funasr_nano.top_p = 0.8F;
    s.conf.model_config.funasr_nano.seed = 42;
    s.conf.model_config.funasr_nano.language = "";
    s.conf.model_config.funasr_nano.itn = 1;
}

void setupQwen3Asr(const BenchConfig &cfg, OfflineSetup &s)
{
    s.conf.model_config.qwen3_asr.conv_frontend =
        s.pool.store(modelFile(cfg.modelDir, "conv_frontend.onnx"));
    s.conf.model_config.qwen3_asr.encoder =
        s.pool.store(modelFile(cfg.modelDir, "encoder.int8.onnx"));
    s.conf.model_config.qwen3_asr.decoder =
        s.pool.store(modelFile(cfg.modelDir, "decoder.int8.onnx"));
    s.conf.model_config.qwen3_asr.tokenizer =
        s.pool.store(modelFile(cfg.modelDir, "tokenizer"));
    s.conf.model_config.qwen3_asr.max_total_len = 1024;
    s.conf.model_config.qwen3_asr.max_new_tokens = 256;
    s.conf.model_config.qwen3_asr.temperature = 1e-6F;
    s.conf.model_config.qwen3_asr.top_p = 0.8F;
    s.conf.model_config.qwen3_asr.seed = 42;
}

// ── Online transducer (streaming) ──────────────────────────────────

QString runOnlineTransducer(const BenchConfig &cfg, double *elapsedSec)
{
    const QString encoder = modelFile(cfg.modelDir, "encoder.int8.onnx");
    const QString decoder = modelFile(cfg.modelDir, "decoder.onnx");
    const QString joiner = modelFile(cfg.modelDir, "joiner.int8.onnx");
    const QString tokens = modelFile(cfg.modelDir, "tokens.txt");

    if (!requireFile(encoder) || !requireFile(decoder) ||
        !requireFile(joiner) || !requireFile(tokens))
    {
        return {};
    }

    StringPool pool;
    QString hotwordsText;
    QByteArray hwBuf;

    if (!cfg.hotwordsPath.isEmpty() && requireFile(cfg.hotwordsPath)) {
        QFile f(cfg.hotwordsPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            hotwordsText = QString::fromUtf8(f.readAll()).trimmed();
        }
        if (!hotwordsText.isEmpty()) {
            hwBuf = hotwordsText.toUtf8();
        }
    }

    SherpaOnnxOnlineRecognizerConfig conf;
    std::memset(&conf, 0, sizeof(conf));
    conf.feat_config.sample_rate = sampleRate;
    conf.feat_config.feature_dim = 80;
    conf.model_config.transducer.encoder = pool.store(encoder);
    conf.model_config.transducer.decoder = pool.store(decoder);
    conf.model_config.transducer.joiner = pool.store(joiner);
    conf.model_config.tokens = pool.store(tokens);
    conf.model_config.provider = "cpu";
    conf.model_config.num_threads = 2;
    conf.model_config.modeling_unit = "cjkchar";

    if (!hwBuf.isEmpty()) {
        conf.decoding_method = "modified_beam_search";
        conf.hotwords_buf = hwBuf.constData();
        conf.hotwords_buf_size = static_cast<int32_t>(hwBuf.size());
        conf.hotwords_score = 1.5F;
    }
    else {
        conf.decoding_method = "greedy_search";
    }
    conf.max_active_paths = 4;
    conf.enable_endpoint = 1;
    conf.rule1_min_trailing_silence = 2.4F;
    conf.rule2_min_trailing_silence = 1.2F;
    conf.rule3_min_utterance_length = 20.0F;

    const SherpaOnnxOnlineRecognizer *rec =
        SherpaOnnxCreateOnlineRecognizer(&conf);
    if (!rec) {
        std::cerr << "Failed to create online recognizer" << std::endl;
        return {};
    }

    const SherpaOnnxOnlineStream *stream = SherpaOnnxCreateOnlineStream(rec);
    if (!stream) {
        SherpaOnnxDestroyOnlineRecognizer(rec);
        return {};
    }

    QByteArray pcm = decodeAudioToPcm(cfg.audioPath);
    if (pcm.isEmpty()) {
        SherpaOnnxDestroyOnlineStream(stream);
        SherpaOnnxDestroyOnlineRecognizer(rec);
        return {};
    }

    QElapsedTimer timer;
    timer.start();

    const int sampleCount = pcm.size() / 2;
    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(sampleCount));
    const auto *data = reinterpret_cast<const uchar *>(pcm.constData());
    for (int i = 0; i < sampleCount; ++i) {
        qint16 s = qFromLittleEndian<qint16>(data + i * 2);
        samples.push_back(static_cast<float>(s) / 32768.0F);
    }

    constexpr int chunk = sampleRate;
    QStringList segments;
    QString lastPartial;

    for (size_t off = 0; off < samples.size(); off += chunk) {
        int cnt = static_cast<int>(
            std::min(static_cast<size_t>(chunk), samples.size() - off));
        if (cnt == 0) {
            break;
        }
        SherpaOnnxOnlineStreamAcceptWaveform(stream, sampleRate,
                                             samples.data() + off, cnt);

        while (SherpaOnnxIsOnlineStreamReady(rec, stream)) {
            SherpaOnnxDecodeOnlineStream(rec, stream);
        }

        const SherpaOnnxOnlineRecognizerResult *res =
            SherpaOnnxGetOnlineStreamResult(rec, stream);
        if (res) {
            QString partial =
                QString::fromUtf8(res->text ? res->text : "").trimmed();
            if (!partial.isEmpty() && partial != lastPartial) {
                lastPartial = partial;
            }
            SherpaOnnxDestroyOnlineRecognizerResult(res);
        }

        if (SherpaOnnxOnlineStreamIsEndpoint(rec, stream)) {
            const SherpaOnnxOnlineRecognizerResult *r2 =
                SherpaOnnxGetOnlineStreamResult(rec, stream);
            if (r2) {
                QString fin =
                    QString::fromUtf8(r2->text ? r2->text : "").trimmed();
                if (!fin.isEmpty()) {
                    segments.append(fin);
                }
                SherpaOnnxDestroyOnlineRecognizerResult(r2);
            }
            SherpaOnnxOnlineStreamReset(rec, stream);
            lastPartial.clear();
        }
    }

    SherpaOnnxOnlineStreamInputFinished(stream);
    while (SherpaOnnxIsOnlineStreamReady(rec, stream)) {
        SherpaOnnxDecodeOnlineStream(rec, stream);
    }

    const SherpaOnnxOnlineRecognizerResult *r3 =
        SherpaOnnxGetOnlineStreamResult(rec, stream);
    if (r3) {
        QString fin = QString::fromUtf8(r3->text ? r3->text : "").trimmed();
        if (!fin.isEmpty() && (segments.isEmpty() || segments.last() != fin)) {
            segments.append(fin);
        }
        SherpaOnnxDestroyOnlineRecognizerResult(r3);
    }

    *elapsedSec = static_cast<double>(timer.nsecsElapsed()) / 1e9;

    SherpaOnnxDestroyOnlineStream(stream);
    SherpaOnnxDestroyOnlineRecognizer(rec);

    return segments.join("");
}

// ── Online Paraformer (streaming) ─────────────────────────────

QString runOnlineParaformer(const BenchConfig &cfg, double *elapsedSec)
{
    const QString encoder = modelFile(cfg.modelDir, "encoder.int8.onnx");
    const QString decoder = modelFile(cfg.modelDir, "decoder.int8.onnx");
    const QString tokens = modelFile(cfg.modelDir, "tokens.txt");

    if (!requireFile(encoder) || !requireFile(decoder) || !requireFile(tokens))
    {
        return {};
    }

    StringPool pool;
    QString hotwordsText;
    QByteArray hwBuf;

    if (!cfg.hotwordsPath.isEmpty() && requireFile(cfg.hotwordsPath)) {
        QFile f(cfg.hotwordsPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            hotwordsText = QString::fromUtf8(f.readAll()).trimmed();
        }
        if (!hotwordsText.isEmpty()) {
            hwBuf = hotwordsText.toUtf8();
        }
    }

    SherpaOnnxOnlineRecognizerConfig conf;
    std::memset(&conf, 0, sizeof(conf));
    conf.feat_config.sample_rate = sampleRate;
    conf.feat_config.feature_dim = 80;
    conf.model_config.paraformer.encoder = pool.store(encoder);
    conf.model_config.paraformer.decoder = pool.store(decoder);
    conf.model_config.tokens = pool.store(tokens);
    conf.model_config.provider = "cpu";
    conf.model_config.num_threads = 2;
    conf.model_config.modeling_unit = "cjkchar";

    if (!hwBuf.isEmpty()) {
        conf.hotwords_buf = hwBuf.constData();
        conf.hotwords_buf_size = static_cast<int32_t>(hwBuf.size());
        conf.hotwords_score = 1.5F;
    }
    conf.decoding_method = "greedy_search";
    conf.max_active_paths = 4;
    conf.enable_endpoint = 1;
    conf.rule1_min_trailing_silence = 2.4F;
    conf.rule2_min_trailing_silence = 1.2F;
    conf.rule3_min_utterance_length = 20.0F;

    const SherpaOnnxOnlineRecognizer *rec =
        SherpaOnnxCreateOnlineRecognizer(&conf);
    if (!rec) {
        std::cerr << "Failed to create online recognizer" << std::endl;
        return {};
    }

    const SherpaOnnxOnlineStream *stream = SherpaOnnxCreateOnlineStream(rec);
    if (!stream) {
        SherpaOnnxDestroyOnlineRecognizer(rec);
        return {};
    }

    QByteArray pcm = decodeAudioToPcm(cfg.audioPath);
    if (pcm.isEmpty()) {
        SherpaOnnxDestroyOnlineStream(stream);
        SherpaOnnxDestroyOnlineRecognizer(rec);
        return {};
    }

    QElapsedTimer timer;
    timer.start();

    const int sampleCount = pcm.size() / 2;
    std::vector<float> samples;
    samples.reserve(static_cast<size_t>(sampleCount));
    const auto *data = reinterpret_cast<const uchar *>(pcm.constData());
    for (int i = 0; i < sampleCount; ++i) {
        qint16 s = qFromLittleEndian<qint16>(data + i * 2);
        samples.push_back(static_cast<float>(s) / 32768.0F);
    }

    constexpr int chunk = sampleRate;
    QStringList segments;
    QString lastPartial;

    for (size_t off = 0; off < samples.size(); off += chunk) {
        int cnt = static_cast<int>(
            std::min(static_cast<size_t>(chunk), samples.size() - off));
        if (cnt == 0) {
            break;
        }
        SherpaOnnxOnlineStreamAcceptWaveform(stream, sampleRate,
                                             samples.data() + off, cnt);

        while (SherpaOnnxIsOnlineStreamReady(rec, stream)) {
            SherpaOnnxDecodeOnlineStream(rec, stream);
        }

        const SherpaOnnxOnlineRecognizerResult *res =
            SherpaOnnxGetOnlineStreamResult(rec, stream);
        if (res) {
            QString partial =
                QString::fromUtf8(res->text ? res->text : "").trimmed();
            if (!partial.isEmpty() && partial != lastPartial) {
                lastPartial = partial;
            }
            SherpaOnnxDestroyOnlineRecognizerResult(res);
        }

        if (SherpaOnnxOnlineStreamIsEndpoint(rec, stream)) {
            const SherpaOnnxOnlineRecognizerResult *r2 =
                SherpaOnnxGetOnlineStreamResult(rec, stream);
            if (r2) {
                QString fin =
                    QString::fromUtf8(r2->text ? r2->text : "").trimmed();
                if (!fin.isEmpty()) {
                    segments.append(fin);
                }
                SherpaOnnxDestroyOnlineRecognizerResult(r2);
            }
            SherpaOnnxOnlineStreamReset(rec, stream);
            lastPartial.clear();
        }
    }

    SherpaOnnxOnlineStreamInputFinished(stream);
    while (SherpaOnnxIsOnlineStreamReady(rec, stream)) {
        SherpaOnnxDecodeOnlineStream(rec, stream);
    }

    const SherpaOnnxOnlineRecognizerResult *r3 =
        SherpaOnnxGetOnlineStreamResult(rec, stream);
    if (r3) {
        QString fin = QString::fromUtf8(r3->text ? r3->text : "").trimmed();
        if (!fin.isEmpty() && (segments.isEmpty() || segments.last() != fin)) {
            segments.append(fin);
        }
        SherpaOnnxDestroyOnlineRecognizerResult(r3);
    }

    *elapsedSec = static_cast<double>(timer.nsecsElapsed()) / 1e9;

    SherpaOnnxDestroyOnlineStream(stream);
    SherpaOnnxDestroyOnlineRecognizer(rec);

    return segments.join("");
}

// ── Offline helper ────────────────────────────────────────────

QString runOffline(const BenchConfig &cfg,
                   void (*setup)(const BenchConfig &, OfflineSetup &),
                   double *elapsedSec, double *loadSecOut = nullptr)
{
    OfflineSetup s;
    std::memset(&s.conf, 0, sizeof(s.conf));
    s.conf.feat_config.sample_rate = sampleRate;
    s.conf.feat_config.feature_dim = 80;

    setup(cfg, s);

    QString tokens = modelFile(cfg.modelDir, "tokens.txt");
    if (QFileInfo::exists(tokens)) {
        s.conf.model_config.tokens = s.pool.store(tokens);
    }

    s.conf.model_config.provider = "cpu";
    s.conf.model_config.num_threads = 2;
    s.conf.model_config.modeling_unit = "cjkchar";
    s.conf.decoding_method = "greedy_search";
    s.conf.max_active_paths = 4;

    // Hotwords for offline models
    bool hasHotwords = false;
    if (!cfg.hotwordsPath.isEmpty() && requireFile(cfg.hotwordsPath)) {
        QFile f(cfg.hotwordsPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString hw = QString::fromUtf8(f.readAll()).trimmed();
            if (!hw.isEmpty()) {
                // Qwen3-ASR uses inline comma-separated hotwords
                // (greedy_search) Others use hotwords_file (requires
                // modified_beam_search)
                if (cfg.type == "qwen3_asr") {
                    // Qwen3-ASR uses comma-separated hotwords instead of
                    // newlines
                    s.conf.model_config.qwen3_asr.hotwords = s.pool.store(
                        QStringList(hw.split('\n', Qt::SkipEmptyParts))
                            .join(','));
                }
                else {
                    s.hwFile.setFileTemplate(
                        QDir::temp().filePath("hw_XXXXXX.txt"));
                    if (s.hwFile.open()) {
                        s.hwFile.write(hw.toUtf8());
                        s.hwFile.flush();
                        s.conf.hotwords_file = s.pool.store(
                            QDir::toNativeSeparators(s.hwFile.fileName()));
                        s.conf.hotwords_score = 1.5F;
                        hasHotwords = true;
                    }
                }
            }
        }
    }
    if (hasHotwords) {
        s.conf.decoding_method = "modified_beam_search";
    }

    QElapsedTimer loadTimer;
    loadTimer.start();
    const SherpaOnnxOfflineRecognizer *rec =
        SherpaOnnxCreateOfflineRecognizer(&s.conf);
    double loadSec = static_cast<double>(loadTimer.nsecsElapsed()) / 1e9;
    if (!rec) {
        std::cerr << "Failed to create offline recognizer (load=" << loadSec
                  << "s)" << std::endl;
        return {};
    }
    if (loadSecOut) {
        *loadSecOut = loadSec;
    }

    QByteArray pcm = decodeAudioToPcm(cfg.audioPath);
    if (pcm.isEmpty()) {
        SherpaOnnxDestroyOfflineRecognizer(rec);
        return {};
    }

    std::vector<float> samples = pcm16ToFloat(pcm);

    QElapsedTimer timer;
    timer.start();

    int chunkSamples = sampleRate * 60;
    if (cfg.type == "moonshine_v2") {
        chunkSamples = sampleRate * 30;
    }
    else if (cfg.type == "funasr_nano") {
        chunkSamples = sampleRate * 10;
    }
    QStringList transcript;

    for (size_t off = 0; off < samples.size(); off += chunkSamples) {
        int cnt = static_cast<int>(
            std::min(static_cast<size_t>(chunkSamples), samples.size() - off));
        if (cnt <= 0) {
            continue;
        }

        const SherpaOnnxOfflineStream *stream =
            SherpaOnnxCreateOfflineStream(rec);
        if (!stream) {
            continue;
        }

        SherpaOnnxAcceptWaveformOffline(stream, sampleRate,
                                        samples.data() + off, cnt);
        SherpaOnnxDecodeOfflineStream(rec, stream);

        const SherpaOnnxOfflineRecognizerResult *res =
            SherpaOnnxGetOfflineStreamResult(stream);
        if (res) {
            QString text =
                QString::fromUtf8(res->text ? res->text : "").trimmed();
            if (!text.isEmpty()) {
                transcript.append(text);
            }
            SherpaOnnxDestroyOfflineRecognizerResult(res);
        }
        SherpaOnnxDestroyOfflineStream(stream);
    }

    *elapsedSec = static_cast<double>(timer.nsecsElapsed()) / 1e9;

    SherpaOnnxDestroyOfflineRecognizer(rec);
    return transcript.join("");
}

// ── Ground truth comparison ───────────────────────────────────

void compareWithGroundTruth(const QString &hypothesis, const QString &gtPath)
{
    QFile f(gtPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::cerr << "Cannot open ground truth file: " << gtPath.toStdString()
                  << std::endl;
        return;
    }

    QStringList gtLines;
    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (!line.isEmpty()) {
            gtLines.append(line);
        }
    }
    QString groundTruth = gtLines.join("");

    std::cout << std::endl;
    std::cout << "─ Ground Truth ──────────────────────" << std::endl;
    std::cout << groundTruth.toStdString() << std::endl;
    std::cout << "─ Recognized ────────────────────────" << std::endl;
    std::cout << hypothesis.toStdString() << std::endl;
    std::cout << "─────────────────────────────────────" << std::endl;
}

} // anonymous namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    BenchConfig cfg = parseArgs(app.arguments());

    QString result;
    double elapsedSec = 0;
    double loadSec = 0;

    if (cfg.type == "transducer") {
        result = runOnlineTransducer(cfg, &elapsedSec);
    }
    else if (cfg.type == "paraformer") {
        result = runOnlineParaformer(cfg, &elapsedSec);
    }
    else if (cfg.type == "sense_voice") {
        result = runOffline(cfg, setupSenseVoice, &elapsedSec, &loadSec);
    }
    else if (cfg.type == "fire_red_asr_ctc") {
        result = runOffline(cfg, setupFireRedAsrCtc, &elapsedSec, &loadSec);
    }
    else if (cfg.type == "fire_red_asr_aed") {
        result = runOffline(cfg, setupFireRedAsrAed, &elapsedSec, &loadSec);
    }
    else if (cfg.type == "moonshine_v2") {
        result = runOffline(cfg, setupMoonshineV2, &elapsedSec, &loadSec);
    }
    else if (cfg.type == "funasr_nano") {
        result = runOffline(cfg, setupFunasrNano, &elapsedSec, &loadSec);
    }
    else if (cfg.type == "qwen3_asr") {
        result = runOffline(cfg, setupQwen3Asr, &elapsedSec, &loadSec);
    }
    else {
        std::cerr << "Unknown model type: " << cfg.type.toStdString()
                  << std::endl;
        usage();
        return 1;
    }

    std::cout << std::endl;
    std::cout << "Benchmark [" << cfg.type.toStdString() << "]" << std::endl;
    std::cout << "  Model dir : "
              << QDir::toNativeSeparators(cfg.modelDir).toStdString()
              << std::endl;
    std::cout << "  Audio     : "
              << QDir::toNativeSeparators(cfg.audioPath).toStdString()
              << std::endl;
    if (cfg.type == "transducer" || cfg.type == "paraformer") {
        std::cout << "  Load time : N/A (online)" << std::endl;
        std::cout << "  Total time: " << elapsedSec << " s" << std::endl;
    }
    else {
        std::cout << "  Load time : " << loadSec << " s" << std::endl;
        std::cout << "  Infer time: " << elapsedSec << " s" << std::endl;
        std::cout << "  Total time: " << (loadSec + elapsedSec) << " s"
                  << std::endl;
    }
    std::cout << "  Transcript: " << result.toStdString() << std::endl;

    if (!cfg.groundTruthPath.isEmpty()) {
        compareWithGroundTruth(result, cfg.groundTruthPath);
    }

    return 0;
}
