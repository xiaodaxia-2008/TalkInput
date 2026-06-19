#include "asr_service.h"
#include "app_config.h"
#include "logging.h"
#include "utils.h"

#include <QDir>
#include <QStringList>
#include <QThread>
#include <QtGlobal>

#include <optional>

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

QString buildHotwordsText(const QString &raw, bool hotwordsSupport)
{
    const QStringList lines = hotwordLines(raw);
    if (lines.isEmpty() || !hotwordsSupport) {
        return {};
    }

    return lines.join(QLatin1Char('\n'));
}

std::optional<talkinput::SpeechRecognizer::Type>
typeFromString(const QString &str)
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
    if (str == QStringLiteral("System")) {
        return talkinput::SpeechRecognizer::Type::System;
    }
    return std::nullopt;
}

bool systemSpeechRecognizerSupported()
{
#if defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}

// Look up an ASR preset in the live appConfig by its model directory name.
std::optional<nlohmann::json>
findAsrPresetJsonByDirectory(const QString &modelDir)
{
    const QString dirName = QDir(modelDir).dirName();
    const nlohmann::json presets = talkinput::appConfigValue("/asrPresets");
    if (!presets.is_array()) {
        return std::nullopt;
    }
    for (const auto &preset : presets) {
        if (preset.is_object() && preset.value("modelDirName", std::string()) ==
                                      dirName.toStdString())
        {
            return std::optional<nlohmann::json>{preset};
        }
    }
    return std::nullopt;
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

void AsrService::setModelType(const QString &type)
{
    m_modelType = type.trimmed();
}

void AsrService::loadModel()
{
    if (m_modelType == QStringLiteral("System")) {
        if (!systemSpeechRecognizerSupported()) {
            SPDLOG_WARN("AsrService: system speech recognizer is unavailable");
            emit modelLoadResult(
                false, tr("System speech recognition is not available on this "
                          "platform."));
            return;
        }
        loadFromJson({}, SpeechRecognizer::Type::System);
        return;
    }

    if (m_modelDir.isEmpty()) {
        SPDLOG_WARN("AsrService: cannot load model, directory not set");
        emit modelLoadResult(false, tr("Model directory not set."));
        return;
    }

    const auto modelPresetJson = findAsrPresetJsonByDirectory(m_modelDir);
    if (!modelPresetJson) {
        SPDLOG_WARN("AsrService: no preset found for {}", m_modelDir);
        emit modelLoadResult(false,
                             tr("No preset found for the selected model."));
        return;
    }

    const QString typeName =
        QString::fromStdString(modelPresetJson->value("type", std::string()));
    const auto type = typeFromString(typeName);
    if (!type) {
        SPDLOG_WARN("AsrService: unsupported recognizer type {}", typeName);
        emit modelLoadResult(false, tr("Unsupported model type."));
        return;
    }

    nlohmann::json config = *modelPresetJson;
    config["modelDir"] = m_modelDir;

    // Resolve absolute paths for all configured model files
    const nlohmann::json files =
        config.value("files", nlohmann::json::object());
    nlohmann::json resolvedFiles = nlohmann::json::object();
    for (auto it = files.begin(); it != files.end(); ++it) {
        if (!it->is_string()) {
            continue;
        }
        const QString relative = QString::fromStdString(it->get<std::string>());
        const QString absolute = QDir(m_modelDir).filePath(relative);
        resolvedFiles[it.key()] = absolute.toStdString();
    }
    config["files"] = resolvedFiles;

    // The nested postPunctuationModel block (if any) is forwarded as-is; the
    // recognizer resolves its model path internally after ASR results.

    const bool hotwordsSupport = config.value("hotwordsSupport", false);
    {
        const nlohmann::json hw = appConfigValue("/settings/asr/hotwords");
        QString raw;
        if (hw.is_array()) {
            QStringList lines;
            for (const auto &item : hw) {
                if (item.is_string()) {
                    const QString s =
                        QString::fromStdString(item.get<std::string>())
                            .trimmed();
                    if (!s.isEmpty()) lines.append(s);
                }
            }
            raw = lines.join(QLatin1Char('\n'));
        }
        else if (hw.is_string()) {
            raw = QString::fromStdString(hw.get<std::string>());
        }
        config["hotwordsText"] =
            buildHotwordsText(raw, hotwordsSupport).toStdString();
    }

    SPDLOG_INFO("AsrService: configured recognizer {}", typeName);

    loadFromJson(config, *type);
}

void AsrService::loadFromJson(const nlohmann::json &config,
                              SpeechRecognizer::Type type)
{
    unloadModel();

    auto recognizer = createSpeechRecognizer(type);
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
    m_streamingMode = m_recognizer->isStreaming();
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
    SPDLOG_INFO("AsrService: model unloaded");
}

void AsrService::startSession()
{
    if (!m_modelLoaded) {
        SPDLOG_WARN("AsrService: startSession called but model not loaded");
        return;
    }

    if (m_recognizer) {
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
        if (m_recognizer->acceptsExternalAudio()) {
            m_recognizer->resetStream();
        }
    }
}

void AsrService::abortSession()
{
    if (m_modelLoaded && m_recognizer && m_recognizer->isRunning()) {
        if (m_recognizer->acceptsExternalAudio()) {
            m_recognizer->resetStream();
        }
        else {
            m_recognizer->finish();
        }
    }
}

bool AsrService::acceptsExternalAudio() const
{
    return !m_recognizer || m_recognizer->acceptsExternalAudio();
}

} // namespace talkinput
