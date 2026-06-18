#include "speech_recognizer.h"

#include "logging.h"
#include "model_registry.h"
#include "utils.h"
#include "recognizers/funasr_nano_speech_recognizer.h"
#include "recognizers/sense_voice_speech_recognizer.h"
#include "recognizers/streaming_paraformer_speech_recognizer.h"
#include "system_speech_recognizer.h"

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

const FileRule *findFileRule(const ModelPreset &preset, const char *configField)
{
    for (const FileRule &rule : preset.files) {
        if (rule.configField == configField) {
            return &rule;
        }
    }
    return nullptr;
}

QString modelsCacheDir()
{
    return QDir(talkinput::appDataDir()).filePath(QStringLiteral("models"));
}

QString configuredToolPath(const QString &toolDir, const char *configField)
{
    if (toolDir.trimmed().isEmpty()) {
        return {};
    }

    const QDir dir(toolDir);
    const auto preset = findToolPresetByDirName(dir.dirName().toStdString());
    if (!preset) {
        return {};
    }

    const FileRule *rule = findFileRule(*preset, configField);
    if (!rule || rule->relativePath.empty()) {
        return {};
    }

    return dir.filePath(QString::fromStdString(rule->relativePath));
}

QString configuredPunctuationModelPath(const SpeechRecognizer::Config &config)
{
    if (!config.punctuationModelDir.trimmed().isEmpty()) {
        return configuredToolPath(config.punctuationModelDir,
                                  "punctuationModelFile");
    }

    if (config.modelDir.trimmed().isEmpty()) {
        return {};
    }

    const auto preset = findModelPresetByDirectory(config.modelDir);
    if (!preset || preset->postPunctuationModelDirName.empty()) {
        return {};
    }

    const QString punctDir = QDir(modelsCacheDir())
                                 .filePath(QString::fromStdString(
                                     preset->postPunctuationModelDirName));
    return configuredToolPath(punctDir, "punctuationModelFile");
}

} // namespace

SpeechRecognizer::SpeechRecognizer(QObject *parent) : QObject(parent)
{
}

SpeechRecognizer::~SpeechRecognizer()
{
    stopPunctuation();
}

bool SpeechRecognizer::prepareRecognizer(const Config &config,
                                         QString *errorMessage)
{
    stopPunctuation();

    if (config.modelDir.trimmed().isEmpty()) {
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

    SherpaOnnxOfflinePunctuationConfig punctConfig;
    std::memset(&punctConfig, 0, sizeof(punctConfig));
    const std::string punctModelStr = punctPath.toUtf8().toStdString();
    punctConfig.model.ct_transformer = punctModelStr.c_str();
    punctConfig.model.num_threads = std::max(1, config.numThreads);
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

bool SpeechRecognizer::configuredModelPath(const Config &config,
                                           const char *configField,
                                           QString *path, QString *errorMessage)
{
    const auto preset = findModelPresetByDirectory(config.modelDir);
    if (!preset) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("No preset found for the selected model.");
        }
        return false;
    }

    const FileRule *rule = findFileRule(*preset, configField);
    if (!rule || rule->relativePath.empty()) {
        if (errorMessage) {
            *errorMessage =
                QStringLiteral("Model preset does not configure %1.")
                    .arg(QString::fromLatin1(configField));
        }
        return false;
    }

    const QString resolved =
        modelPath(config.modelDir, QString::fromStdString(rule->relativePath));
    const bool exists = rule->isDir ? pathExists(resolved, errorMessage)
                                    : fileExists(resolved, errorMessage);
    if (!exists) {
        return false;
    }

    if (path) {
        *path = resolved;
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
