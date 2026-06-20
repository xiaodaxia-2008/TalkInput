#include "audio_file_decoder.h"
#include "audio_utils.h"
#include "logging.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>

namespace talkinput
{

std::expected<DecodedAudioFile, QString>
decodeAudioFileToPcm16(const QString &path, int timeoutMs)
{
    QAudioDecoder decoder;
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);

    DecodedAudioFile decoded;
    bool ok = false;
    QString error;

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &decoder, [&]() {
        const QAudioBuffer buf = decoder.read();
        const QAudioFormat format = buf.format();
        if (decoded.sampleRate == 0) {
            decoded.sampleRate = format.sampleRate();
            decoded.channels = format.channelCount();
        }
        else if (decoded.sampleRate != format.sampleRate() ||
                 decoded.channels != format.channelCount())
        {
            SPDLOG_WARN("Audio decoder format changed from {} channels {} "
                        "to {} channels {}",
                        decoded.sampleRate, decoded.channels,
                        format.sampleRate(), format.channelCount());
        }

        const QByteArray audioData(buf.constData<char>(), buf.byteCount());
        decoded.pcm16.append(convertAudioToPcm16(audioData, format));
    });

    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, [&]() {
        ok = true;
        loop.quit();
    });

    QObject::connect(&decoder,
                     static_cast<void (QAudioDecoder::*)(QAudioDecoder::Error)>(
                         &QAudioDecoder::error),
                     &loop, [&](QAudioDecoder::Error) {
                         error = decoder.errorString();
                         SPDLOG_ERROR("Audio decoder error: {}", error);
                         loop.quit();
                     });

    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    decoder.setSource(QUrl::fromLocalFile(path));
    decoder.start();

    timeoutTimer.start(timeoutMs);
    loop.exec();

    decoder.stop();

    if (!ok || decoded.pcm16.isEmpty()) {
        return std::unexpected(error);
    }

    if (decoded.sampleRate <= 0 || decoded.channels <= 0) {
        return std::unexpected(error);
    }

    return decoded;
}

} // namespace talkinput
