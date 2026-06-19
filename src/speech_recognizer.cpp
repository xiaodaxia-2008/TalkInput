#include "speech_recognizer.h"

#include "app_config.h"
#include "logging.h"
#include "recognizers/funasr_nano_speech_recognizer.h"
#include "recognizers/sense_voice_speech_recognizer.h"
#include "recognizers/streaming_paraformer_speech_recognizer.h"
#include "system_speech_recognizer.h"
#include "utils.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <QDir>
#include <QFileInfo>
#include <QStringList>
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

bool SpeechRecognizer::prepareRecognizer(const nlohmann::json &config,
                                         QString *errorMessage)
{
    stopPunctuation();

    const QString modelDir = jsonString(config, "modelDir");
    if (modelDir.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Model directory is empty.");
        }
        return false;
    }

    const QString punctPath = configuredPunctuationModelPath(config);
    if (punctPath.isEmpty()) {
        return true;
    }

    if (!QFileInfo::exists(punctPath)) {
        SPDLOG_WARN("Punctuation model not found: {}", punctPath);
        return true;
    }

    const int numThreads = jsonInt(config.value("params", nlohmann::json::object()),
                                   "numThreads", 2);

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

    return true;
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

bool SpeechRecognizer::configuredModelPath(const nlohmann::json &config,
                                           const char *configField,
                                           QString *path, QString *errorMessage)
{
    const QString resolved = jsonString(
        config.value("files", nlohmann::json::object()), configField);
    if (resolved.isEmpty()) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Model file not configured: %1")
                    .arg(QString::fromLatin1(configField));
        }
        return false;
    }

    const QString modelDir = jsonString(config, "modelDir");
    const QString absolutePath = modelPath(modelDir, resolved);

    if (!QFileInfo::exists(absolutePath)) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Required model file is missing: %1")
                    .arg(QDir::toNativeSeparators(absolutePath));
        }
        return false;
    }

    if (path) {
        *path = absolutePath;
    }
    return true;
}

bool SpeechRecognizer::fileExists(const QString &path, QString *errorMessage)
{
    const QFileInfo info(path);
    if (info.exists() && info.isFile()) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Required model file is missing: %1")
                            .arg(QDir::toNativeSeparators(path));
    }
    return false;
}

bool SpeechRecognizer::pathExists(const QString &path, QString *errorMessage)
{
    const QFileInfo info(path);
    if (info.exists()) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Required model path is missing: %1")
                            .arg(QDir::toNativeSeparators(path));
    }
    return false;
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
    if (str == QStringLiteral("SenseVoice"))
        return SpeechRecognizer::Type::SenseVoice;
    if (str == QStringLiteral("FunASRNano"))
        return SpeechRecognizer::Type::FunASRNano;
    if (str == QStringLiteral("StreamingParaformer"))
        return SpeechRecognizer::Type::StreamingParaformer;
    if (str == QStringLiteral("System"))
        return SpeechRecognizer::Type::System;
    return std::nullopt;
}

QStringList hotwordLines(const QString &raw)
{
    QStringList lines;
    for (QString line : raw.split(QLatin1Char('\n'))) {
        line.remove(QLatin1Char('\r'));
        line = line.trimmed();
        if (!line.isEmpty()) lines.append(line);
    }
    return lines;
}

QString buildHotwordsText(const QString &raw, bool hotwordsSupport)
{
    const QStringList lines = hotwordLines(raw);
    if (lines.isEmpty() || !hotwordsSupport) return {};
    return lines.join(QLatin1Char('\n'));
}

QString hotwordsFromConfig(const nlohmann::json &hotwordsConfig)
{
    QStringList lines;
    if (hotwordsConfig.is_array()) {
        for (const auto &item : hotwordsConfig) {
            if (item.is_string()) {
                const QString s =
                    QString::fromStdString(item.get<std::string>()).trimmed();
                if (!s.isEmpty()) lines.append(s);
            }
        }
    }
    return lines.join(QLatin1Char('\n'));
}

bool systemSpeechRecognizerSupported()
{
#if defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}

} // namespace

std::unique_ptr<SpeechRecognizer>
SpeechRecognizer::createFromConfig(const nlohmann::json &preset,
                                   const QString &modelDir,
                                   const nlohmann::json &hotwordsConfig,
                                   QString *errorMessage, QObject *parent)
{
    const QString typeName = jsonString(preset, "type");
    const auto type = typeFromString(typeName);
    if (!type) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Unsupported model type: %1").arg(typeName);
        return nullptr;
    }

    // System recognizer is a special case — no files, no directory
    if (*type == SpeechRecognizer::Type::System) {
        if (!systemSpeechRecognizerSupported()) {
            if (errorMessage)
                *errorMessage = QStringLiteral(
                    "System speech recognition is not available on this platform.");
            return nullptr;
        }
        auto r = createSpeechRecognizer(*type, parent);
        if (!r) return nullptr;
        if (!r->start(nlohmann::json::object(), errorMessage)) return nullptr;
        return r;
    }

    if (modelDir.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Model directory not set.");
        return nullptr;
    }

    nlohmann::json config = preset;
    config["modelDir"] = modelDir;

    // Resolve absolute paths for model files
    const nlohmann::json files = config.value("files", nlohmann::json::object());
    nlohmann::json resolvedFiles = nlohmann::json::object();
    for (auto it = files.begin(); it != files.end(); ++it) {
        if (!it->is_string()) continue;
        const QString relative = QString::fromStdString(it->get<std::string>());
        resolvedFiles[it.key()] = QDir(modelDir).filePath(relative).toStdString();
    }
    config["files"] = resolvedFiles;

    // Inject hotwords
    const bool hotwordsSupport = config.value("hotwordsSupport", false);
    config["hotwordsText"] =
        buildHotwordsText(hotwordsFromConfig(hotwordsConfig), hotwordsSupport)
            .toStdString();

    auto r = createSpeechRecognizer(*type, parent);
    if (!r) return nullptr;
    if (!r->start(config, errorMessage)) return nullptr;
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
