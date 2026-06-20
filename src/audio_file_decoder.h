#pragma once

#include <QByteArray>
#include <QString>

#include <expected>

namespace talkinput
{

struct DecodedAudioFile
{
    QByteArray pcm16;
    int sampleRate = 0;
    int channels = 0;
};

std::expected<DecodedAudioFile, QString>
decodeAudioFileToPcm16(const QString &path, int timeoutMs = 30000);

} // namespace talkinput
