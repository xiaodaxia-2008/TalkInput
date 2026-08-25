#include "app_config.h"
#include "audio_utils.h"
#include "logging.h"
#include "speech_recognizer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QThread>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{

struct RecognitionRun
{
    QString presetId;
    QString modelName;
    QString audioFile;
};

void runTranscription(const QString &presetId, const QString &audioPath)
{
    const auto &presets = zenny::appConfig().asrPresets;
    const auto it = presets.find(presetId.toStdString());
    if (it == presets.end()) {
        SPDLOG_ERROR("Preset not found: {}", presetId.toStdString());
        return;
    }

    const auto &preset = it->second;
    SPDLOG_INFO("==================================================");
    SPDLOG_INFO("Model: {} ({})", preset.name, preset.id);
    SPDLOG_INFO("Audio: {}", audioPath.toStdString());

    auto decoded = zenny::decodeAudioFileToPcm16(audioPath);
    if (!decoded) {
        SPDLOG_ERROR("Failed to decode audio file: {}", decoded.error().toStdString());
        return;
    }

    SPDLOG_INFO("Decoded: {} samples, {} Hz, {} ch",
                decoded->pcm16.size() / (2 * decoded->channels),
                decoded->sampleRate, decoded->channels);

    // Create speech recognizer from preset using Zenny top-level factory
    auto recognizerExpected = zenny::SpeechRecognizer::createFromPreset(preset, nullptr, true);
    if (!recognizerExpected) {
        SPDLOG_ERROR("Failed to create recognizer: {}", recognizerExpected.error().toStdString());
        return;
    }

    std::unique_ptr<zenny::SpeechRecognizer> recognizer = std::move(*recognizerExpected);

    QString recognizedText;
    QObject::connect(recognizer.get(), &zenny::SpeechRecognizer::resultChanged,
                     [&recognizedText](const QString &text, bool isFinal) {
                         if (isFinal) {
                             recognizedText = text;
                         }
                     });

    // Feed audio and finish (triggers VAD/silence segmenting and decoding)
    recognizer->acceptPcm16(decoded->pcm16, decoded->sampleRate, decoded->channels);
    recognizer->finish();

    SPDLOG_INFO("Result:\n{}\n", recognizedText.toStdString());
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString meetingPath = QDir::current().filePath("data/audio/meeting.m4a");
    const QString fiberartPath = QDir::current().filePath("data/audio/fiberart.m4a");

    const std::vector<std::string> presetIds = {
        "sense-voice-zh-en-ja-ko-yue-int8-2024-07-17",
        "funasr-nano-int8-2025-12-30"
    };

    const std::vector<QString> audioFiles = {
        fiberartPath,
        meetingPath
    };

    for (const auto &presetId : presetIds) {
        for (const auto &audioPath : audioFiles) {
            runTranscription(QString::fromStdString(presetId), audioPath);
        }
    }

    return 0;
}
