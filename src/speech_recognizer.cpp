#include "speech_recognizer.h"

#include "asr_config.h"
#include "logging.h"
#include "recognizers/funasr_nano_speech_recognizer.h"
#include "recognizers/sense_voice_speech_recognizer.h"
#include "recognizers/streaming_paraformer_speech_recognizer.h"
#include "system_speech_recognizer.h"
#include "utils.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <QDir>
#include <QFileInfo>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <optional>

namespace talkinput
{
namespace
{

// Resolve the punctuation model path from the nested postPunctuationModel
// config block that lives inside the recognizer's own preset. The punctuation
// model is downloaded under appDataDir()/models/<modelDirName>/<file>.
QString configuredPunctuationModelPath(const nlohmann::json &config)
{
    const nlohmann::json punct =
        config.value("postPunctuationModel", nlohmann::json::object());
    if (!punct.is_object() || punct.empty()) {
        return {};
    }

    const QString punctDirName = jsonString(punct, "modelDirName");
    if (punctDirName.isEmpty()) {
        return {};
    }

    const nlohmann::json punctFiles =
        punct.value("files", nlohmann::json::object());
    const QString punctFile = jsonString(punctFiles, "punctuationModelFile");
    if (punctFile.isEmpty()) {
        return {};
    }

    const QString punctDir =
        QDir(QDir(appDataDir()).filePath(QStringLiteral("models")))
            .filePath(punctDirName);
    return QDir(punctDir).filePath(punctFile);
}

} // namespace

SpeechRecognizer::SpeechRecognizer(QObject *parent) : QObject(parent)
{
}

SpeechRecognizer::~SpeechRecognizer()
{
    stopPunctuation();
}

std::expected<void, QString>
SpeechRecognizer::prepareRecognizer(const nlohmann::json &config)
{
    stopPunctuation();

    const QString modelDir = jsonString(config, "modelDir");
    if (modelDir.trimmed().isEmpty()) {
        return std::unexpected(QStringLiteral("Model directory is empty."));
    }

    const QString punctPath = configuredPunctuationModelPath(config);
    if (punctPath.isEmpty()) {
        return {};
    }

    if (!QFileInfo::exists(punctPath)) {
        SPDLOG_WARN("Punctuation model not found: {}", punctPath);
        return {};
    }

    const int numThreads = jsonInt(
        config.value("params", nlohmann::json::object()), "numThreads", 2);

    SherpaOnnxOfflinePunctuationConfig punctConfig;
    std::memset(&punctConfig, 0, sizeof(punctConfig));
    const std::string punctModelStr = punctPath.toUtf8().toStdString();
    punctConfig.model.ct_transformer = punctModelStr.c_str();
    punctConfig.model.num_threads = std::max(1, numThreads);
    punctConfig.model.provider = "cpu";

    m_punct = SherpaOnnxCreateOfflinePunctuation(&punctConfig);
    if (!m_punct) {
        SPDLOG_WARN("Failed to create punctuation processor");
    }
    else {
        SPDLOG_INFO("Punctuation model loaded: {}", punctPath);
    }

    return {};
}

void SpeechRecognizer::stopPunctuation()
{
    if (m_punct) {
        SherpaOnnxDestroyOfflinePunctuation(m_punct);
        m_punct = nullptr;
    }
}

QString SpeechRecognizer::addPunctuation(const QString &text) const
{
    if (text.isEmpty() || !m_punct) {
        return text;
    }

    const std::string utf8Text = text.toUtf8().toStdString();
    const char *punctResult =
        SherpaOfflinePunctuationAddPunct(m_punct, utf8Text.c_str());
    if (!punctResult) {
        return text;
    }

    QString result = QString::fromUtf8(punctResult).trimmed();
    SherpaOfflinePunctuationFreeText(punctResult);
    return result.isEmpty() ? text : result;
}

QString SpeechRecognizer::modelPath(const QString &modelDir,
                                    const QString &fileName)
{
    return QDir(modelDir).filePath(fileName);
}

std::expected<QString, QString>
SpeechRecognizer::configuredModelPath(const nlohmann::json &config,
                                      const char *configField)
{
    const QString resolved = jsonString(
        config.value("files", nlohmann::json::object()), configField);
    if (resolved.isEmpty()) {
        return std::unexpected(QStringLiteral("Model file not configured: %1")
                                   .arg(QString::fromLatin1(configField)));
    }

    const QString modelDir = jsonString(config, "modelDir");
    const QString absolutePath = modelPath(modelDir, resolved);

    if (!QFileInfo::exists(absolutePath)) {
        return std::unexpected(
            QStringLiteral("Required model file is missing: %1")
                .arg(QDir::toNativeSeparators(absolutePath)));
    }

    return absolutePath;
}

std::expected<void, QString> SpeechRecognizer::fileExists(const QString &path)
{
    const QFileInfo info(path);
    if (info.exists() && info.isFile()) {
        return {};
    }

    return std::unexpected(QStringLiteral("Required model file is missing: %1")
                               .arg(QDir::toNativeSeparators(path)));
}

std::expected<void, QString> SpeechRecognizer::pathExists(const QString &path)
{
    const QFileInfo info(path);
    if (info.exists()) {
        return {};
    }

    return std::unexpected(QStringLiteral("Required model path is missing: %1")
                               .arg(QDir::toNativeSeparators(path)));
}

QString SpeechRecognizer::decodeSherpaText(const char *text)
{
    return QString::fromUtf8(text ? text : "").trimmed();
}

int SpeechRecognizer::appendPcm16AsMonoFloat(const QByteArray &audioData,
                                             int channelCount,
                                             std::vector<float> *samples)
{
    if (!samples || audioData.isEmpty() || channelCount <= 0) {
        return 0;
    }

    constexpr int bytesPerSample = 2;
    const int bytesPerFrame = bytesPerSample * channelCount;
    const int frameCount = audioData.size() / bytesPerFrame;
    if (frameCount <= 0) {
        return 0;
    }

    samples->reserve(samples->size() + static_cast<size_t>(frameCount));

    const auto *data = reinterpret_cast<const uchar *>(audioData.constData());
    for (int frame = 0; frame < frameCount; ++frame) {
        float mixed = 0.0F;
        const int frameOffset = frame * bytesPerFrame;
        for (int channel = 0; channel < channelCount; ++channel) {
            const int sampleOffset = frameOffset + channel * bytesPerSample;
            const qint16 sample =
                qFromLittleEndian<qint16>(data + sampleOffset);
            mixed += static_cast<float>(sample) / 32768.0F;
        }
        samples->push_back(mixed / static_cast<float>(channelCount));
    }

    return frameCount;
}

bool SpeechRecognizer::acceptsExternalAudio() const
{
    return true;
}

// ---------------------------------------------------------------------------
// Static factory
// ---------------------------------------------------------------------------
namespace
{

std::optional<SpeechRecognizer::Type> typeFromString(const QString &str)
{
    if (str == QStringLiteral("SenseVoice")) {
        return SpeechRecognizer::Type::SenseVoice;
    }
    if (str == QStringLiteral("FunASRNano")) {
        return SpeechRecognizer::Type::FunASRNano;
    }
    if (str == QStringLiteral("StreamingParaformer")) {
        return SpeechRecognizer::Type::StreamingParaformer;
    }
    if (str == QStringLiteral("System")) {
        return SpeechRecognizer::Type::System;
    }
    return std::nullopt;
}

} // namespace

bool systemSpeechRecognizerSupported();

std::expected<std::unique_ptr<SpeechRecognizer>, QString>
SpeechRecognizer::createFromConfig(const nlohmann::json &preset,
                                   const QString &modelDir,
                                   const nlohmann::json &hotwordsConfig,
                                   QObject *parent)
{
    const QString typeName = jsonString(preset, "type");
    const auto type = typeFromString(typeName);
    if (!type) {
        return std::unexpected(
            QStringLiteral("Unsupported model type: %1").arg(typeName));
    }

    // System recognizer is a special case — no files, no directory
    if (*type == SpeechRecognizer::Type::System) {
        if (!systemSpeechRecognizerSupported()) {
            return std::unexpected(
                QStringLiteral("System speech recognition is not available on "
                               "this platform."));
        }
        auto r = createSpeechRecognizer(*type, parent);
        if (!r) {
            return std::unexpected(QString());
        }
        auto startResult = r->start(nlohmann::json::object());
        if (!startResult) {
            return std::unexpected(startResult.error());
        }
        return r;
    }

    if (modelDir.isEmpty()) {
        return std::unexpected(QStringLiteral("Model directory not set."));
    }

    nlohmann::json config = preset;
    config["modelDir"] = modelDir;

    nlohmann::json resolvedFiles = nlohmann::json::object();
    const nlohmann::json files =
        config.value("files", nlohmann::json::object());
    for (auto it = files.begin(); it != files.end(); ++it) {
        if (!it->is_string()) {
            continue;
        }
        const QString relative = QString::fromStdString(it->get<std::string>());
        resolvedFiles[it.key()] =
            QDir(modelDir).filePath(relative).toStdString();
    }
    config["files"] = resolvedFiles;

    config["hotwordsText"] =
        hotwordsTextForPreset(config, hotwordsConfig).toStdString();

    auto r = createSpeechRecognizer(*type, parent);
    if (!r) {
        return std::unexpected(QString());
    }
    auto startResult = r->start(config);
    if (!startResult) {
        return std::unexpected(startResult.error());
    }
    return r;
}

std::unique_ptr<SpeechRecognizer>
createSpeechRecognizer(SpeechRecognizer::Type type, QObject *parent)
{
    switch (type) {
    case SpeechRecognizer::Type::StreamingParaformer:
        return std::make_unique<StreamingParaformerSpeechRecognizer>(parent);
    case SpeechRecognizer::Type::SenseVoice:
        return std::make_unique<SenseVoiceSpeechRecognizer>(parent);
    case SpeechRecognizer::Type::FunASRNano:
        return std::make_unique<FunASRNanoSpeechRecognizer>(parent);
    case SpeechRecognizer::Type::System:
        return std::make_unique<SystemSpeechRecognizer>(parent);
    }

    return nullptr;
}

} // namespace talkinput
