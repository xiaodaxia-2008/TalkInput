#include "app_config.h"
#include "audio_utils.h"
#include "logging.h"
#include "speaker_recognizer.h"
#include "speech_recognizer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{

std::vector<float> pcm16ToFloats(const QByteArray &pcm16, int channels)
{
    if (channels <= 0 || pcm16.isEmpty()) {
        return {};
    }
    const int frameCount = pcm16.size() / (2 * channels);
    std::vector<float> floats(static_cast<size_t>(frameCount));
    const auto *data = reinterpret_cast<const qint16 *>(pcm16.constData());
    for (int i = 0; i < frameCount; ++i) {
        if (channels == 1) {
            floats[static_cast<size_t>(i)] =
                static_cast<float>(data[i]) / 32768.0F;
        }
        else {
            int64_t sum = 0;
            for (int ch = 0; ch < channels; ++ch) {
                sum += data[static_cast<size_t>(i * channels + ch)];
            }
            floats[static_cast<size_t>(i)] =
                static_cast<float>(sum) / (32768.0F * channels);
        }
    }
    return floats;
}

QByteArray floatsToPcm16(std::span<const float> samples)
{
    QByteArray pcm16;
    pcm16.reserve(static_cast<int>(samples.size()) * 2);
    for (float s : samples) {
        const float clamped = std::clamp(s, -1.0F, 1.0F);
        const qint16 sample = static_cast<qint16>(clamped * 32767.0F);
        pcm16.append(reinterpret_cast<const char *>(&sample), 2);
    }
    return pcm16;
}

QString formatTimestamp(int ms)
{
    const int totalSeconds = ms / 1000;
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    const int millis = ms % 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

void processMeetingAudio(const QString &audioPath,
                         talkinput::SpeakerRecognizer &speakerRecognizer,
                         talkinput::SpeechRecognizer &recognizer)
{
    SPDLOG_INFO("==================================================");
    SPDLOG_INFO("Processing meeting file: {}", audioPath.toStdString());

    auto decoded = talkinput::decodeAudioFileToPcm16(audioPath);
    if (!decoded) {
        SPDLOG_ERROR("Failed to decode audio: {}", decoded.error().toStdString());
        return;
    }

    const auto &pcm16 = decoded->pcm16;
    const int sampleRate = decoded->sampleRate;
    const int channels = decoded->channels;
    SPDLOG_INFO("Decoded: {} samples, {}Hz, {}ch ({:.2f}s)",
                pcm16.size() / (2 * channels), sampleRate, channels,
                static_cast<double>(pcm16.size() / (2 * channels)) / sampleRate);

    const auto floats = pcm16ToFloats(pcm16, channels);

    // 1. Speaker Diarization
    SPDLOG_INFO("Running speaker diarization...");
    auto segments = speakerRecognizer.diarizeAudio(floats, sampleRate, 0.55F);
    SPDLOG_INFO("Found {} speaker turns/segments:", segments.size());

    // 2. Transcribe each turn with ASR
    for (size_t i = 0; i < segments.size(); ++i) {
        auto &seg = segments[i];
        const int startSample = static_cast<int>(static_cast<int64_t>(seg.startMs) * sampleRate / 1000);
        const int endSample = std::min(static_cast<int>(floats.size()),
                                       static_cast<int>(static_cast<int64_t>(seg.endMs) * sampleRate / 1000));
        const int count = endSample - startSample;
        if (count <= 0) {
            continue;
        }

        const auto segFloats = std::span(floats).subspan(
            static_cast<size_t>(startSample), static_cast<size_t>(count));
        const QByteArray segPcm = floatsToPcm16(segFloats);

        QString segText;
        auto conn = QObject::connect(&recognizer,
                         &talkinput::SpeechRecognizer::resultChanged,
                         [&segText](const QString &text, bool isFinal) {
                             if (isFinal) {
                                 segText = text;
                             }
                         });

        recognizer.resetStream();
        recognizer.acceptPcm16(segPcm, sampleRate, 1);
        recognizer.finish();
        QObject::disconnect(conn);

        seg.text = segText;

        SPDLOG_INFO("[{} - {}] [{}] {}",
                    formatTimestamp(seg.startMs).toStdString(),
                    formatTimestamp(seg.endMs).toStdString(),
                    seg.speakerName.toStdString(),
                    seg.text.toStdString());
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Initialize Speaker Recognizer
    talkinput::SpeakerRecognizer speakerRecognizer;
    auto initRes = speakerRecognizer.init();
    if (!initRes) {
        SPDLOG_ERROR("Failed to initialize SpeakerRecognizer: {}",
                     initRes.error().toStdString());
        return 1;
    }

    // Prepare default SenseVoice ASR preset
    const auto &presets = talkinput::appConfig().asrPresets;
    auto it = presets.find("sense-voice-zh-en-ja-ko-yue-int8-2024-07-17");
    if (it == presets.end()) {
        it = presets.find("sense-voice");
    }
    if (it == presets.end()) {
        SPDLOG_ERROR("SenseVoice preset not found in config");
        return 1;
    }
    const auto &preset = it->second;

    auto recognizerExpected =
        talkinput::SpeechRecognizer::createFromPreset(preset, nullptr, true);
    if (!recognizerExpected) {
        SPDLOG_ERROR("Failed to create ASR recognizer");
        return 1;
    }
    auto recognizer = std::move(*recognizerExpected);

    const QString meetingPath =
        QDir::current().filePath("data/audio/meeting.m4a");
    const QString fiberartPath =
        QDir::current().filePath("data/audio/fiberart.m4a");

    if (QFileInfo::exists(meetingPath)) {
        processMeetingAudio(meetingPath, speakerRecognizer, *recognizer);
    }

    if (QFileInfo::exists(fiberartPath)) {
        processMeetingAudio(fiberartPath, speakerRecognizer, *recognizer);
    }

    return 0;
}
