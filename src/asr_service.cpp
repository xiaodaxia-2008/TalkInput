#include "asr_service.h"
#include "app_config.h"
#include "logging.h"
#include "model_registry.h"

#include <QDir>
#include <QStringList>
#include <QThread>

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

void AsrService::setPunctuationModelDir(const QString &dir)
{
    m_punctuationModelDir = QDir::fromNativeSeparators(dir.trimmed());
    if (!m_punctuationModelDir.isEmpty()) {
        m_punctuationModelDir = QDir::cleanPath(m_punctuationModelDir);
    }
    SPDLOG_DEBUG("AsrService: punctuation model dir set to {}",
                 m_punctuationModelDir);
}

void AsrService::loadModel()
{
    SpeechRecognizer::Config config;
    config.modelDir = m_modelDir;

    QString typeName = m_modelType;
    std::optional<ModelPreset> modelPreset;
    if (typeName.isEmpty() && !m_modelDir.isEmpty()) {
        modelPreset = findModelPresetByDirectory(m_modelDir);
        if (modelPreset) {
            typeName = QString::fromStdString(modelPreset->type);
        }
    }

    const auto type = typeFromString(typeName);
    if (!type) {
        SPDLOG_WARN("AsrService: unsupported recognizer type {}", typeName);
        emit modelLoadResult(false, tr("Unsupported model type."));
        return;
    }

    config.type = *type;
    if (config.type != SpeechRecognizer::Type::System &&
        config.modelDir.isEmpty())
    {
        SPDLOG_WARN("AsrService: cannot load model, directory not set");
        emit modelLoadResult(false, tr("Model directory not set."));
        return;
    }

    config.language = appConfigString("settings/app/language", "zh");
    config.senseVoiceUseItn = true;
    config.punctuationModelDir = m_punctuationModelDir;
    if (!modelPreset && !m_modelDir.isEmpty()) {
        modelPreset = findModelPresetByDirectory(m_modelDir);
    }
    const bool hotwordsSupport =
        modelPreset ? modelPreset->hotwordsSupport : false;
    config.hotwordsText = buildHotwordsText(
        appConfigString("settings/model/hotwords"), hotwordsSupport);

    SPDLOG_INFO("AsrService: configured recognizer {}", typeName);

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
    // Keep punctuation dir across reloads; cleared by setPunctuationModelDir()
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
