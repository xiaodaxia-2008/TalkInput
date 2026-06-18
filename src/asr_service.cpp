#include "asr_service.h"
#include "app_config.h"
#include "logging.h"
#include "model_registry.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>

namespace
{

QString findModelFile(const QDir &dir, const QStringList &patterns)
{
    const auto files = dir.entryInfoList(patterns, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        return {};
    }
    for (const QFileInfo &fi : files) {
        if (fi.fileName().contains(QStringLiteral("int8"))) {
            return fi.absoluteFilePath();
        }
    }
    return files.first().absoluteFilePath();
}

QString findModelFileOrDir(const QDir &dir, const QStringList &patterns)
{
    const auto files =
        dir.entryInfoList(patterns, QDir::Files | QDir::Dirs, QDir::Name);
    if (files.isEmpty()) {
        return {};
    }
    for (const QFileInfo &fi : files) {
        if (fi.fileName().contains(QStringLiteral("int8"))) {
            return fi.absoluteFilePath();
        }
    }
    return files.first().absoluteFilePath();
}

bool hasFile(const QDir &dir, const QStringList &patterns)
{
    return !dir.entryInfoList(patterns, QDir::Files, QDir::Name).isEmpty();
}

talkinput::SpeechRecognizer::Type detectModelArch(const QString &modelDir)
{
    const QDir dir(modelDir);

    const bool hasJoiner = hasFile(dir, {QStringLiteral("*joiner*")});
    const bool hasEncoder = hasFile(dir, {QStringLiteral("*encoder*")});
    const bool hasDecoder = hasFile(dir, {QStringLiteral("*decoder*")});
    const bool hasTokens =
        QFileInfo(dir.filePath(QStringLiteral("tokens.txt"))).exists();

    const bool hasFunasrFiles =
        hasFile(dir, {QStringLiteral("*encoder_adaptor*")}) &&
        hasFile(dir, {QStringLiteral("*llm*")}) &&
        hasFile(dir, {QStringLiteral("*embedding*")});

    const bool hasQwenFrontend =
        hasFile(dir, {QStringLiteral("*conv*frontend*")});
    const bool hasQwenTok =
        QFileInfo(dir.filePath(QStringLiteral("tokenizer"))).isDir();
    const bool hasModelInt8 = hasFile(dir, {QStringLiteral("model.int8.onnx")});

    if (hasJoiner && hasEncoder && hasDecoder && hasTokens) {
        return talkinput::SpeechRecognizer::Type::StreamingTransducer;
    }

    if (hasFunasrFiles) {
        return talkinput::SpeechRecognizer::Type::FunASRNano;
    }

    if (hasQwenFrontend && hasEncoder && hasDecoder && hasQwenTok) {
        return talkinput::SpeechRecognizer::Type::Qwen3ASR;
    }

    if (hasEncoder && hasDecoder && hasTokens) {
        return talkinput::SpeechRecognizer::Type::StreamingParaformer;
    }

    if (hasModelInt8 && hasTokens) {
        return talkinput::SpeechRecognizer::Type::SenseVoice;
    }

    SPDLOG_WARN("AsrService: unknown model arch in {}; falling back to "
                "streaming transducer",
                modelDir);
    return talkinput::SpeechRecognizer::Type::StreamingTransducer;
}

talkinput::SpeechRecognizer::Type typeFromString(const QString &str)
{
    if (str == QStringLiteral("SenseVoice")) {
        return talkinput::SpeechRecognizer::Type::SenseVoice;
    }
    if (str == QStringLiteral("FunASRNano")) {
        return talkinput::SpeechRecognizer::Type::FunASRNano;
    }
    if (str == QStringLiteral("Qwen3ASR")) {
        return talkinput::SpeechRecognizer::Type::Qwen3ASR;
    }
    if (str == QStringLiteral("StreamingParaformer")) {
        return talkinput::SpeechRecognizer::Type::StreamingParaformer;
    }
    return talkinput::SpeechRecognizer::Type::StreamingTransducer;
}

QStringList hotwordLines(const QString &raw)
{
    QStringList lines;
    for (QString line : raw.split(QLatin1Char('\n'))) {
        line.remove(QLatin1Char('\r'));
        line = line.trimmed();
        if (!line.isEmpty()) {
            lines.append(line);
        }
    }
    return lines;
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

QString buildHotwordsText(const QString &raw,
                          talkinput::SpeechRecognizer::Type type)
{
    const QStringList lines = hotwordLines(raw);
    if (lines.isEmpty()) {
        return {};
    }

    if (type == talkinput::SpeechRecognizer::Type::Qwen3ASR) {
        return lines.join(QLatin1Char(','));
    }

    if (type == talkinput::SpeechRecognizer::Type::FunASRNano) {
        return lines.join(QLatin1Char('\n'));
    }

    if (type == talkinput::SpeechRecognizer::Type::StreamingTransducer) {
        QStringList formatted;
        formatted.reserve(lines.size());
        for (const QString &line : lines) {
            const QString hotword = formatCjkHotwordLine(line);
            if (!hotword.isEmpty()) {
                formatted.append(hotword);
            }
        }
        return formatted.join(QLatin1Char('\n'));
    }

    return {};
}

} // namespace

namespace talkinput
{

AsrService::AsrService(QObject *parent) : QObject(parent)
{
}

AsrService::~AsrService()
{
    unloadModel();
}

void AsrService::setModelDirectory(const QString &dir)
{
    m_modelDir = QDir::fromNativeSeparators(dir.trimmed());
    if (!m_modelDir.isEmpty()) {
        m_modelDir = QDir::cleanPath(m_modelDir);
    }
}

void AsrService::setPunctuationModelDir(const QString &dir)
{
    m_punctuationModelDir = QDir::fromNativeSeparators(dir.trimmed());
    if (!m_punctuationModelDir.isEmpty()) {
        m_punctuationModelDir = QDir::cleanPath(m_punctuationModelDir);
    }
    SPDLOG_DEBUG("AsrService: punctuation model dir set to {}",
                 m_punctuationModelDir);
}

SpeechRecognizer::Config AsrService::detectAndConfigure(const QString &modelDir)
{
    const QDir dir(modelDir);
    SpeechRecognizer::Config config;
    config.modelDir = modelDir;

    // Try the preset registry first
    ModelFileSet resolved = resolveModelFiles(modelDir);
    if (resolved.matched) {
        config.type = typeFromString(QString::fromStdString(resolved.type));
        auto rel = [&](const QString &abs) {
            return dir.relativeFilePath(abs);
        };

        auto resolvedFile = [&](const char *key) {
            const auto it = resolved.resolvedFiles.find(key);
            return it == resolved.resolvedFiles.end()
                       ? QString()
                       : QString::fromStdString(it->second);
        };
        auto assign = [&](const char *key, QString &field) {
            const QString v = resolvedFile(key);
            if (!v.isEmpty()) {
                field = rel(v);
            }
        };
        assign("encoderFile", config.encoderFile);
        assign("decoderFile", config.decoderFile);
        assign("joinerFile", config.joinerFile);
        assign("tokensFile", config.tokensFile);
        assign("senseVoiceModelFile", config.senseVoiceModelFile);
        assign("funasrEncoderAdaptorFile", config.funasrEncoderAdaptorFile);
        assign("funasrLlmFile", config.funasrLlmFile);
        assign("funasrEmbeddingFile", config.funasrEmbeddingFile);
        assign("qwen3ConvFrontendFile", config.qwen3ConvFrontendFile);
        assign("qwen3EncoderFile", config.qwen3EncoderFile);
        assign("qwen3DecoderFile", config.qwen3DecoderFile);

        // Directory fields (not relative to modelDir — already absolute or
        // relative)
        {
            const QString tok = resolvedFile("funasrTokenizerFile");
            if (!tok.isEmpty()) {
                config.funasrTokenizerFile = dir.relativeFilePath(tok);
            }
            else {
                config.funasrTokenizerFile = QStringLiteral("Qwen3-0.6B");
            }
        }
        {
            const QString tok = resolvedFile("qwen3TokenizerDir");
            if (!tok.isEmpty()) {
                config.qwen3TokenizerDir = tok; // absolute
            }
        }

        // Non-file model config
        config.senseVoiceLanguage =
            appConfigString("settings/app/language", "zh");
        config.senseVoiceUseItn = true;

        SPDLOG_INFO("AsrService: configured from preset {}", resolved.type);
    }
    else {
        // Fall back to file-probing detection
        const auto arch = detectModelArch(modelDir);
        config.type = arch;

        switch (arch) {
        case SpeechRecognizer::Type::StreamingTransducer: {
            auto rel = [&](const QString &abs) {
                return dir.relativeFilePath(abs);
            };
            config.encoderFile =
                rel(findModelFile(dir, {QStringLiteral("*encoder*")}));
            config.decoderFile =
                rel(findModelFile(dir, {QStringLiteral("*decoder*")}));
            config.joinerFile =
                rel(findModelFile(dir, {QStringLiteral("*joiner*")}));
            break;
        }
        case SpeechRecognizer::Type::StreamingParaformer: {
            auto rel = [&](const QString &abs) {
                return dir.relativeFilePath(abs);
            };
            config.encoderFile =
                rel(findModelFile(dir, {QStringLiteral("*encoder*")}));
            config.decoderFile =
                rel(findModelFile(dir, {QStringLiteral("*decoder*")}));
            break;
        }
        case SpeechRecognizer::Type::SenseVoice: {
            auto rel = [&](const QString &abs) {
                return dir.relativeFilePath(abs);
            };
            config.senseVoiceModelFile =
                rel(findModelFile(dir, {QStringLiteral("model.int8.onnx")}));
            config.senseVoiceLanguage =
                appConfigString("settings/app/language", "zh");
            config.senseVoiceUseItn = true;
            break;
        }
        case SpeechRecognizer::Type::FunASRNano: {
            auto rel = [&](const QString &abs) {
                return dir.relativeFilePath(abs);
            };
            config.funasrEncoderAdaptorFile =
                rel(findModelFile(dir, {QStringLiteral("*encoder_adaptor*")}));
            config.funasrLlmFile =
                rel(findModelFile(dir, {QStringLiteral("*llm*")}));
            config.funasrEmbeddingFile =
                rel(findModelFile(dir, {QStringLiteral("*embedding*")}));
            const auto tokDir =
                findModelFileOrDir(dir, {QStringLiteral("*Qwen3*"),
                                         QStringLiteral("*tokenizer*")});
            if (!tokDir.isEmpty()) {
                config.funasrTokenizerFile = dir.relativeFilePath(tokDir);
            }
            else {
                config.funasrTokenizerFile = QStringLiteral("Qwen3-0.6B");
            }
            break;
        }
        case SpeechRecognizer::Type::Qwen3ASR: {
            auto rel = [&](const QString &abs) {
                return dir.relativeFilePath(abs);
            };
            config.qwen3ConvFrontendFile =
                rel(findModelFile(dir, {QStringLiteral("*conv*frontend*")}));
            config.qwen3EncoderFile =
                rel(findModelFile(dir, {QStringLiteral("*encoder*")}));
            config.qwen3DecoderFile =
                rel(findModelFile(dir, {QStringLiteral("*decoder*")}));
            const QString tokDir =
                findModelFileOrDir(dir, {QStringLiteral("tokenizer")});
            config.qwen3TokenizerDir = tokDir.isEmpty() ? modelDir : tokDir;
            break;
        }
        }
    }

    // Look for punctuation model if configured
    const QString punctModelPath = findPunctuationModelPath(modelDir);
    if (!punctModelPath.isEmpty()) {
        config.punctuationModelPath = punctModelPath;
        SPDLOG_DEBUG("AsrService: using punctuation model: {}", punctModelPath);
    }

    config.hotwordsText = buildHotwordsText(
        appConfigString("settings/model/hotwords"), config.type);

    return config;
}

QString AsrService::findPunctuationModelPath(const QString &modelDir) const
{
    // 1. Check if explicitly set via setPunctuationModelDir()
    if (!m_punctuationModelDir.isEmpty()) {
        const QDir dir(m_punctuationModelDir);
        const QStringList onnxFiles =
            dir.entryList({QStringLiteral("*.onnx")}, QDir::Files, QDir::Name);
        if (!onnxFiles.isEmpty()) {
            return dir.absoluteFilePath(onnxFiles.first());
        }
        return {};
    }

    // 2. Check the preset registry for a configured punctuation partner
    const std::string dirName = QDir(modelDir).dirName().toStdString();
    for (const auto &preset : loadModelPresets()) {
        if (preset.modelDirName == dirName &&
            !preset.postPunctuationModelDirName.empty())
        {
            const QString punctDirName =
                QString::fromStdString(preset.postPunctuationModelDirName);
            const QString cacheBase =
                QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
            const QString punctDir =
                QDir(cacheBase).filePath("models/" + punctDirName);
            if (!QFileInfo(punctDir).isDir()) {
                return {};
            }
            const QDir pDir(punctDir);
            const QStringList onnxFiles = pDir.entryList(
                {QStringLiteral("*.onnx")}, QDir::Files, QDir::Name);
            if (!onnxFiles.isEmpty()) {
                return pDir.absoluteFilePath(onnxFiles.first());
            }
            break;
        }
    }

    return {};
}

void AsrService::loadModel()
{
    if (m_modelDir.isEmpty()) {
        SPDLOG_WARN("AsrService: cannot load model, directory not set");
        emit modelLoadResult(false, tr("Model directory not set."));
        return;
    }

    SpeechRecognizer::Config config = detectAndConfigure(m_modelDir);
    const bool streamingMode =
        (config.type == SpeechRecognizer::Type::StreamingTransducer ||
         config.type == SpeechRecognizer::Type::StreamingParaformer);

    unloadModel();

    auto recognizer = createSpeechRecognizer(config.type);
    if (!recognizer) {
        m_modelLoaded = false;
        emit modelLoadResult(false, tr("Unsupported model type."));
        return;
    }

    QString error;
    if (!recognizer->start(config, &error)) {
        SPDLOG_ERROR("AsrService: model load failed: {}", error);
        m_modelLoaded = false;
        emit modelLoadResult(false, error);
        return;
    }

    connect(recognizer.get(), &SpeechRecognizer::resultChanged, this,
            &AsrService::resultChanged);
    m_recognizer = std::move(recognizer);
    m_modelLoaded = true;
    m_streamingMode = streamingMode;
    const char *mode = m_streamingMode ? "streaming" : "offline";
    SPDLOG_INFO("AsrService: {} model loaded from {}", mode, m_modelDir);
    emit modelLoadResult(true, {});
}

void AsrService::unloadModel()
{
    if (m_recognizer && m_recognizer->isRunning()) {
        m_recognizer->stop();
    }
    m_recognizer.reset();
    m_modelLoaded = false;
    m_streamingMode = false;
    // Keep punctuation dir across reloads; cleared by setPunctuationModelDir()
    SPDLOG_INFO("AsrService: model unloaded");
}

void AsrService::startSession()
{
    if (!m_modelLoaded) {
        SPDLOG_WARN("AsrService: startSession called but model not loaded");
        return;
    }

    if (m_recognizer && m_recognizer->isRunning()) {
        m_recognizer->resetStream();
    }
}

void AsrService::feedAudio(const QByteArray &pcm16, int sampleRate,
                           int channels)
{
    if (!m_modelLoaded) {
        return;
    }
    if (m_recognizer) {
        m_recognizer->acceptPcm16(pcm16, sampleRate, channels);
    }
}

void AsrService::finishSession()
{
    if (!m_modelLoaded) {
        return;
    }

    if (m_recognizer && m_recognizer->isRunning()) {
        m_recognizer->finish();
        m_recognizer->resetStream();
    }
}

void AsrService::abortSession()
{
    if (m_modelLoaded && m_recognizer && m_recognizer->isRunning()) {
        m_recognizer->resetStream();
    }
}

} // namespace talkinput
