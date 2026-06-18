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

QString buildHotwordsText(const QString &raw,
                          talkinput::SpeechRecognizer::Type type)
{
    const QStringList lines = hotwordLines(raw);
    if (lines.isEmpty()) {
        return {};
    }

    if (type == talkinput::SpeechRecognizer::Type::FunASRNano) {
        return lines.join(QLatin1Char('\n'));
    }

    return {};
}

talkinput::SpeechRecognizer::Type typeFromString(const QString &str)
{
    if (str == QStringLiteral("SenseVoice")) {
        return talkinput::SpeechRecognizer::Type::SenseVoice;
    }
    if (str == QStringLiteral("FunASRNano")) {
        return talkinput::SpeechRecognizer::Type::FunASRNano;
    }
    if (str == QStringLiteral("StreamingParaformer")) {
        return talkinput::SpeechRecognizer::Type::StreamingParaformer;
    }
    return talkinput::SpeechRecognizer::Type::StreamingParaformer;
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

    // Resolve model config from preset registry
    ModelFileSet resolved = resolveModelFiles(m_modelDir);
    if (!resolved.matched) {
        SPDLOG_WARN("AsrService: no preset found for model directory: {}",
                    m_modelDir);
        emit modelLoadResult(false,
                             tr("No preset found for the selected model."));
        return;
    }

    SpeechRecognizer::Config config;
    config.modelDir = m_modelDir;
    config.type = typeFromString(QString::fromStdString(resolved.type));
    const bool streamingMode =
        (config.type == SpeechRecognizer::Type::StreamingParaformer);

    const QDir dir(m_modelDir);
    auto rel = [&](const QString &abs) { return dir.relativeFilePath(abs); };
    auto resolvedFile = [&](const char *key) -> QString {
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
    assign("tokensFile", config.tokensFile);
    assign("senseVoiceModelFile", config.senseVoiceModelFile);
    assign("funasrEncoderAdaptorFile", config.funasrEncoderAdaptorFile);
    assign("funasrLlmFile", config.funasrLlmFile);
    assign("funasrEmbeddingFile", config.funasrEmbeddingFile);

    {
        const QString tok = resolvedFile("funasrTokenizerFile");
        if (!tok.isEmpty()) {
            config.funasrTokenizerFile = dir.relativeFilePath(tok);
        }
        else {
            config.funasrTokenizerFile = QStringLiteral("Qwen3-0.6B");
        }
    }

    config.senseVoiceLanguage = appConfigString("settings/app/language", "zh");
    config.senseVoiceUseItn = true;

    // Punctuation model
    config.punctuationModelPath = findPunctuationModelPath(m_modelDir);
    if (!config.punctuationModelPath.isEmpty()) {
        SPDLOG_DEBUG("AsrService: using punctuation model: {}",
                     config.punctuationModelPath);
    }

    config.hotwordsText = buildHotwordsText(
        appConfigString("settings/model/hotwords"), config.type);

    SPDLOG_INFO("AsrService: configured from preset {}", resolved.type);

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
