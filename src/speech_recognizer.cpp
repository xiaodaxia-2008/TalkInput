#include "speech_recognizer.h"

#include "app_config.h"
#include "audio_utils.h"
#include "logging.h"
#include "recognizers/funasr_nano_speech_recognizer.h"
#include "recognizers/sense_voice_speech_recognizer.h"
#include "recognizers/streaming_paraformer_speech_recognizer.h"
#include "utils.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <QAudioDevice>
#include <QAudioSource>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QMediaDevices>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <optional>

namespace talkinput
{

SpeechRecognizer::SpeechRecognizer(QObject *parent) : QObject(parent)
{
}

SpeechRecognizer::~SpeechRecognizer()
{
    stopCapture();
    stopPunctuation();
}

std::expected<void, QString>
SpeechRecognizer::prepareRecognizer(const AsrPreset &preset)
{
    stopPunctuation();

    if (preset.resolvedModelDir.empty()) {
        return std::unexpected(QStringLiteral("Model directory is empty."));
    }

    auto it = preset.resolvedFiles.find("punctuationModelFile");
    if (it == preset.resolvedFiles.end()) {
        return {};
    }

    const QString punctPath = QString::fromStdString(it->second);
    if (!QFileInfo::exists(punctPath)) {
        SPDLOG_WARN("Punctuation model not found: {}", punctPath);
        return {};
    }

    const int numThreads = std::max(1, preset.params.numThreads);

    SherpaOnnxOfflinePunctuationConfig punctConfig;
    std::memset(&punctConfig, 0, sizeof(punctConfig));
    const std::string punctModelStr = punctPath.toUtf8().toStdString();
    punctConfig.model.ct_transformer = punctModelStr.c_str();
    punctConfig.model.num_threads = numThreads;
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

// ── Audio capture ─────────────────────────────────────────────────

std::expected<void, QString> SpeechRecognizer::startCapture()
{
    if (m_audioSource) {
        return {};
    }

    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        SPDLOG_ERROR("No audio input device");
        return std::unexpected(tr("No microphone available"));
    }

    m_audioFormat = inputDevice.preferredFormat();
    if (!m_audioFormat.isValid() ||
        m_audioFormat.sampleFormat() == QAudioFormat::Unknown)
    {
        m_audioFormat = QAudioFormat();
        m_audioFormat.setSampleRate(48000);
        m_audioFormat.setChannelCount(1);
        m_audioFormat.setSampleFormat(QAudioFormat::Int16);
    }

    if (!inputDevice.isFormatSupported(m_audioFormat)) {
        SPDLOG_ERROR("Audio format not supported");
        return std::unexpected(tr("Microphone format not supported."));
    }

    m_audioSource = std::make_unique<QAudioSource>(inputDevice, m_audioFormat);
    m_audioDevice = m_audioSource->start();
    if (!m_audioDevice) {
        SPDLOG_ERROR("Failed to start microphone");
        m_audioSource.reset();
        return std::unexpected(tr("Failed to start microphone"));
    }

    connect(m_audioDevice, &QIODevice::readyRead, this, [this]() {
        if (!m_audioDevice) {
            return;
        }
        const QByteArray audioData = m_audioDevice->readAll();
        const QByteArray pcm16 = convertAudioToPcm16(audioData, m_audioFormat);
        if (!pcm16.isEmpty()) {
            acceptPcm16(pcm16, m_audioFormat.sampleRate(),
                        m_audioFormat.channelCount());
        }
    });

    return {};
}

void SpeechRecognizer::stopCapture()
{
    if (m_audioSource) {
        m_audioSource->stop();
    }
    m_audioDevice = nullptr;
    m_audioSource.reset();
}

bool SpeechRecognizer::isCaptureRunning() const
{
    return m_audioSource != nullptr;
}

// ── Static factory ────────────────────────────────────────────────
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
    return std::nullopt;
}

} // namespace

std::expected<std::unique_ptr<SpeechRecognizer>, QString>
SpeechRecognizer::createFromPreset(const AsrPreset &preset,
                                   QObject *parent)
{
    const auto type = typeFromString(QString::fromStdString(preset.type));
    if (!type) {
        return std::unexpected(
            QStringLiteral("Unsupported model type: %1")
                .arg(QString::fromStdString(preset.type)));
    }

    AsrPreset resolved = preset;
    const QString dirName = QString::fromStdString(preset.modelDirName);
    if (dirName.isEmpty()) {
        return std::unexpected(QStringLiteral("Model directory not set."));
    }
    const QString modelDir =
        QDir(appDataDir())
            .filePath(QStringLiteral("models/%1").arg(dirName));
    resolved.resolvedModelDir = modelDir.toStdString();

    for (const auto &[key, relative] : preset.files) {
        resolved.resolvedFiles[key] =
            QDir(modelDir).filePath(QString::fromStdString(relative))
                .toStdString();
    }

    if (preset.hotwordsSupport) {
        QStringList lines;
        for (const auto &item : appConfig().settings.hotwords) {
            const QString line =
                QString::fromStdString(item).trimmed();
            if (!line.isEmpty()) {
                lines.append(line);
            }
        }
        resolved.hotwordsText =
            lines.join(QLatin1Char('\n')).toStdString();
    }

    std::unique_ptr<SpeechRecognizer> r;
    switch (*type) {
    case SpeechRecognizer::Type::StreamingParaformer:
        r = std::make_unique<StreamingParaformerSpeechRecognizer>(parent);
        break;
    case SpeechRecognizer::Type::SenseVoice:
        r = std::make_unique<SenseVoiceSpeechRecognizer>(parent);
        break;
    case SpeechRecognizer::Type::FunASRNano:
        r = std::make_unique<FunASRNanoSpeechRecognizer>(parent);
        break;
    }
    if (!r) {
        return std::unexpected(QString());
    }
    auto startResult = r->start(resolved);
    if (!startResult) {
        return std::unexpected(startResult.error());
    }
    return r;
}

} // namespace talkinput
