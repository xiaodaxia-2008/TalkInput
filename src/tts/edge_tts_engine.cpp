#include "edge_tts_engine.h"

#include "../app_config.h"
#include "../logging.h"
#include "tts_audio.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVector>
#include <QWebSocket>

#include <algorithm>
#include <cstdint>

namespace talkinput
{

namespace
{

constexpr const char *kTrustedClientToken = "6A5AA1D4EAFF4E9FB37E23D68491D6F4";
constexpr const char *kWssBaseUrl =
    "wss://speech.platform.bing.com/consumer/speech/synthesize/readaloud/"
    "edge/v1?TrustedClientToken=6A5AA1D4EAFF4E9FB37E23D68491D6F4";
constexpr const char *kSecMsGecVersion = "1-143.0.3650.75";
constexpr const char *kChromiumUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36 Edg/143.0.0.0";
constexpr const char *kOrigin =
    "chrome-extension://jdiccldimpdaibmpdkjnbmckianbfold";
constexpr const char *kOutputFormat = "audio-24khz-48kbitrate-mono-mp3";

// Unix seconds between 1601-01-01 (Windows file time epoch) and 1970-01-01.
constexpr qint64 kWinEpochSecs = 11644473600LL;
constexpr qint64 kSecMsGecBucketSecs = 300;
constexpr int kMaxChunkBytes = 4000;
constexpr int kConnectTimeoutMs = 30000;
constexpr int kReceiveTimeoutMs = 60000;
constexpr int kDecodeTimeoutMs = 30000;
constexpr int kTargetRate = 24000;

const char *kDefaultOpenAiVoices[] = {"alloy", "echo", "fable",
                                      "onyx",  "nova", "shimmer"};

bool isDefaultOpenAiVoice(const QString &voice)
{
    for (const char *name : kDefaultOpenAiVoices) {
        if (voice.compare(QLatin1String(name), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString jsStyleTimestamp()
{
    const QDateTime utc = QDateTime::currentDateTimeUtc();
    const QLocale cLocale = QLocale::c();
    return cLocale.toString(utc,
                            QStringLiteral("ddd MMM dd yyyy HH:mm:ss "
                                           "'GMT+0000 (Coordinated Universal "
                                           "Time)'"));
}

QString generateSecMsGec()
{
    const qint64 unixSecs = QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
    qint64 ticks = unixSecs + kWinEpochSecs;
    ticks -= ticks % kSecMsGecBucketSecs;
    const qint64 hundredNs = ticks * 10000000LL;
    const QByteArray toHash =
        QByteArray::number(hundredNs) + kTrustedClientToken;
    return QString::fromLatin1(
        QCryptographicHash::hash(toHash, QCryptographicHash::Sha256)
            .toHex()
            .toUpper());
}

QString randomHexId()
{
    return QUuid::createUuid()
        .toString(QUuid::WithoutBraces)
        .remove(QLatin1Char('-'));
}

/// Converts a short voice id like "zh-CN-XiaoxiaoNeural" into the long form
/// the service expects: "Microsoft Server Speech Text to Speech Voice
/// (zh-CN, XiaoxiaoNeural)". Unrecognized names pass through unchanged.
QString toEdgeVoiceName(QString voice)
{
    static const QRegularExpression kShortVoice(
        QStringLiteral("^([a-z]{2,})-([A-Z]{2,})-(.+Neural)$"));
    const QRegularExpressionMatch match = kShortVoice.match(voice);
    if (match.hasMatch()) {
        QString lang = match.captured(1);
        QString region = match.captured(2);
        QString name = match.captured(3);
        if (name.contains(QLatin1Char('-'))) {
            region +=
                QLatin1Char('-') + name.left(name.indexOf(QLatin1Char('-')));
            name = name.mid(name.indexOf(QLatin1Char('-')) + 1);
        }
        voice = QStringLiteral("Microsoft Server Speech Text to Speech Voice "
                               "(%1-%2, %3)")
                    .arg(lang, region, name);
    }
    return voice;
}

QString escapeXmlText(const QString &text)
{
    QString out;
    out.reserve(text.size() + 16);
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'&':
            out += QStringLiteral("&amp;");
            break;
        case u'<':
            out += QStringLiteral("&lt;");
            break;
        case u'>':
            out += QStringLiteral("&gt;");
            break;
        case u'"':
            out += QStringLiteral("&quot;");
            break;
        case u'\'':
            out += QStringLiteral("&apos;");
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

QString removeIncompatibleCharacters(const QString &text)
{
    QString out = text;
    for (QChar &c : out) {
        const ushort code = c.unicode();
        if (code <= 8 || (code >= 11 && code <= 12) ||
            (code >= 14 && code <= 31))
        {
            c = QLatin1Char(' ');
        }
    }
    return out;
}

/// Splits @p text into chunks whose UTF-8 representation stays at or below
/// @p maxBytes, never cutting a multi-byte character.
QList<QString> splitByUtf8Bytes(const QString &text, int maxBytes)
{
    const QByteArray utf8 = text.toUtf8();
    QList<QString> chunks;
    int pos = 0;
    while (pos < utf8.size()) {
        int end = pos + maxBytes;
        if (end >= utf8.size()) {
            end = utf8.size();
        }
        else {
            // Back up to the start of a UTF-8 sequence.
            while (end > pos && (utf8.at(end) & 0xC0) == 0x80) {
                --end;
            }
        }
        chunks.append(QString::fromUtf8(utf8.mid(pos, end - pos).trimmed()));
        pos = end;
    }
    chunks.removeAll(QString());
    return chunks;
}

/// Appends one decoded buffer's samples (mixed to mono) to @p mono.
void appendBufferAsMonoFloats(const QAudioBuffer &audioBuffer,
                              QVector<float> &mono)
{
    const QAudioFormat format = audioBuffer.format();
    const int channels = qMax(1, format.channelCount());
    const qsizetype frames = audioBuffer.frameCount();
    if (channels <= 0 || frames <= 0) {
        return;
    }
    mono.reserve(static_cast<qsizetype>(mono.size()) + frames);
    switch (format.sampleFormat()) {
    case QAudioFormat::Float: {
        const float *src = audioBuffer.constData<float>();
        if (!src) {
            return;
        }
        for (qsizetype f = 0; f < frames; ++f) {
            float sum = 0;
            for (int c = 0; c < channels; ++c) {
                sum += src[f * channels + c];
            }
            mono.append(sum / channels);
        }
        break;
    }
    case QAudioFormat::Int16: {
        const qint16 *src = audioBuffer.constData<qint16>();
        if (!src) {
            return;
        }
        for (qsizetype f = 0; f < frames; ++f) {
            float sum = 0;
            for (int c = 0; c < channels; ++c) {
                sum += src[f * channels + c] / 32768.0f;
            }
            mono.append(sum / channels);
        }
        break;
    }
    case QAudioFormat::Int32: {
        const qint32 *src = audioBuffer.constData<qint32>();
        if (!src) {
            return;
        }
        for (qsizetype f = 0; f < frames; ++f) {
            float sum = 0;
            for (int c = 0; c < channels; ++c) {
                sum += src[f * channels + c] / 2147483648.0f;
            }
            mono.append(sum / channels);
        }
        break;
    }
    case QAudioFormat::UInt8: {
        const quint8 *src = audioBuffer.constData<quint8>();
        if (!src) {
            return;
        }
        for (qsizetype f = 0; f < frames; ++f) {
            float sum = 0;
            for (int c = 0; c < channels; ++c) {
                sum += (static_cast<float>(src[f * channels + c]) - 128.0f) /
                       128.0f;
            }
            mono.append(sum / channels);
        }
        break;
    }
    default:
        break;
    }
}

/// Decodes MP3 bytes into 24 kHz 16-bit mono PCM.
QByteArray decodeMp3ToPcm24k(const QByteArray &mp3, QString *error)
{
    if (mp3.isEmpty()) {
        *error = QStringLiteral("received empty audio data");
        return {};
    }

    QBuffer source;
    source.setData(mp3);
    source.open(QIODevice::ReadOnly);

    QAudioDecoder decoder;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(kDecodeTimeoutMs);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    QVector<float> mono;
    int sampleRate = 0;

    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&]() {
        const QAudioBuffer audioBuffer = decoder.read();
        if (!audioBuffer.isValid()) {
            return;
        }
        const QAudioFormat format = audioBuffer.format();
        if (format.sampleFormat() == QAudioFormat::Unknown) {
            return;
        }
        if (sampleRate == 0) {
            sampleRate = format.sampleRate();
        }
        appendBufferAsMonoFloats(audioBuffer, mono);
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop,
                     &QEventLoop::quit);
    QObject::connect(&decoder,
                     QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error),
                     &loop, [&](QAudioDecoder::Error decoderError) {
                         if (decoderError != QAudioDecoder::NoError) {
                             *error = decoder.errorString();
                             loop.quit();
                         }
                     });

    // Ask for the exact format we need; backends that cannot honor it emit
    // their native format instead, which appendBufferAsMonoFloats normalizes.
    QAudioFormat wanted;
    wanted.setSampleRate(kTargetRate);
    wanted.setChannelCount(1);
    wanted.setSampleFormat(QAudioFormat::Int16);
    decoder.setAudioFormat(wanted);
    decoder.setSourceDevice(&source);
    decoder.start();

    timeout.start();
    loop.exec();
    decoder.stop();

    if (!error->isEmpty()) {
        return {};
    }
    if (mono.isEmpty()) {
        *error = QStringLiteral("decoder produced no audio");
        return {};
    }
    if (sampleRate <= 0) {
        *error = QStringLiteral("decoder reported an invalid sample rate");
        return {};
    }
    return resampleFloatToInt16(mono.constData(), mono.size(), sampleRate,
                                kTargetRate);
}

} // namespace

TtsSynthesisResult EdgeTtsEngine::synthesize(const QString &text,
                                             const QString &voice, double speed)
{
    TtsSynthesisResult result;

    QString selectedVoice = voice.trimmed();
    if (selectedVoice.isEmpty() || isDefaultOpenAiVoice(selectedVoice)) {
        selectedVoice =
            QString::fromStdString(appConfig().settings.ttsEdgeVoice);
    }
    if (selectedVoice.isEmpty()) {
        result.error = QStringLiteral("No edge-tts voice configured.");
        return result;
    }
    selectedVoice = toEdgeVoiceName(selectedVoice);

    const double clampedSpeed = std::clamp(speed, 0.25, 4.0);
    const int ratePct =
        std::clamp(static_cast<int>((clampedSpeed - 1.0) * 100), -50, 300);

    const QList<QString> chunks =
        splitByUtf8Bytes(removeIncompatibleCharacters(text), kMaxChunkBytes);
    if (chunks.isEmpty()) {
        result.error = QStringLiteral("Cannot synthesize empty text.");
        return result;
    }

    for (const QString &chunk : chunks) {
        QWebSocket socket{QLatin1String(kOrigin)};

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        timeout.setInterval(kReceiveTimeoutMs);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

        bool failed = false;
        QString failReason;
        bool turnEnded = false;
        QByteArray chunkMp3;
        int ssmlSent = 0;

        const auto sendSynthesisRequest = [&]() {
            const QString config =
                QStringLiteral(
                    "X-Timestamp:%1\r\n"
                    "Content-Type:application/json; charset=utf-8\r\n"
                    "Path:speech.config\r\n\r\n"
                    "{\"context\":{\"synthesis\":{\"audio\":{"
                    "\"metadataoptions\":{\"sentenceBoundaryEnabled\":\"true\","
                    "\"wordBoundaryEnabled\":\"false\"},"
                    "\"outputFormat\":\"%2\"}}}}\r\n")
                    .arg(jsStyleTimestamp(), QLatin1String(kOutputFormat));
            socket.sendTextMessage(config);

            const QString requestId = randomHexId();
            const QString ssml =
                QStringLiteral(
                    "<speak version='1.0' "
                    "xmlns='http://www.w3.org/2001/10/synthesis' "
                    "xml:lang='en-US'>"
                    "<voice name='%1'>"
                    "<prosody pitch='+0Hz' rate='%2%' volume='+0%'>%3"
                    "</prosody></voice></speak>")
                    .arg(selectedVoice)
                    .arg(ratePct)
                    .arg(escapeXmlText(chunk));
            socket.sendTextMessage(
                QStringLiteral("X-RequestId:%1\r\n"
                               "Content-Type:application/ssml+xml\r\n"
                               "X-Timestamp:%2Z\r\n"
                               "Path:ssml\r\n\r\n%3")
                    .arg(requestId, jsStyleTimestamp(), ssml));
            ++ssmlSent;
            timeout.start();
        };

        QObject::connect(&socket, &QWebSocket::connected, &loop,
                         [&]() { sendSynthesisRequest(); });

        QObject::connect(
            &socket, &QWebSocket::binaryMessageReceived, &loop,
            [&](const QByteArray &message) {
                timeout.start();
                if (message.size() < 2) {
                    return;
                }
                const quint16 headerLength = static_cast<quint16>(
                    (static_cast<quint8>(message.at(0)) << 8) |
                    static_cast<quint8>(message.at(1)));
                if (headerLength > message.size() - 2) {
                    return;
                }
                const QByteArray headerBytes = message.mid(2, headerLength);
                bool isAudio = false;
                for (const QByteArray &line : headerBytes.split('\n')) {
                    const qsizetype colon = line.indexOf(':');
                    if (colon < 0) {
                        continue;
                    }
                    if (line.left(colon).trimmed().compare(
                            "Path", Qt::CaseInsensitive) == 0 &&
                        line.mid(colon + 1).trimmed().compare(
                            "audio", Qt::CaseInsensitive) == 0)
                    {
                        isAudio = true;
                        break;
                    }
                }
                if (isAudio) {
                    chunkMp3.append(message.mid(2 + headerLength));
                }
            });

        QObject::connect(
            &socket, &QWebSocket::textMessageReceived, &loop,
            [&](const QString &message) {
                timeout.start();
                const QString path = [&]() -> QString {
                    const qsizetype end =
                        message.indexOf(QStringLiteral("\r\n\r\n"));
                    const QString header =
                        end < 0 ? message : message.left(end);
                    for (const QString &line :
                         header.split(QLatin1Char('\n'))) {
                        const qsizetype colon = line.indexOf(QLatin1Char(':'));
                        if (colon < 0) {
                            continue;
                        }
                        if (line.left(colon).trimmed().compare(
                                QStringLiteral("Path"), Qt::CaseInsensitive) ==
                            0)
                        {
                            return line.mid(colon + 1).trimmed();
                        }
                    }
                    return QString();
                }();
                if (path.compare(QStringLiteral("turn.end"),
                                 Qt::CaseInsensitive) == 0)
                {
                    turnEnded = true;
                    loop.quit();
                }
                else if (path.compare(QStringLiteral("response"),
                                      Qt::CaseInsensitive) == 0)
                {
                    // Metadata/error response; ignore unless it is an error.
                    const qsizetype end =
                        message.indexOf(QStringLiteral("\r\n\r\n"));
                    const QByteArray data =
                        end < 0 ? QByteArray() : message.mid(end + 4).toUtf8();
                    SPDLOG_DEBUG("edge-tts: response frame: {}",
                                 QString::fromUtf8(data));
                    const QJsonDocument doc = QJsonDocument::fromJson(data);
                    if (doc.isObject()) {
                        const QJsonObject obj = doc.object();
                        const QString name =
                            obj.value(QLatin1String("context"))
                                .toObject()
                                .value(QLatin1String("serviceTag"))
                                .toString();
                        if (!name.isEmpty()) {
                            SPDLOG_DEBUG("edge-tts: response {}", name);
                        }
                        const QString error =
                            obj.value(QLatin1String("error"))
                                .toObject()
                                .value(QLatin1String("message"))
                                .toString();
                        if (!error.isEmpty()) {
                            failed = true;
                            failReason =
                                QStringLiteral("server error: %1").arg(error);
                            loop.quit();
                        }
                    }
                }
            });

        QObject::connect(&socket, &QWebSocket::disconnected, &loop, [&]() {
            if (!turnEnded) {
                failed = true;
                failReason = socket.errorString();
                SPDLOG_DEBUG("edge-tts: disconnected before turn.end, "
                             "errorCode={} errorString={}",
                             static_cast<int>(socket.error()),
                             socket.errorString());
                loop.quit();
            }
        });

        QObject::connect(&socket,
                         QOverload<QAbstractSocket::SocketError>::of(
                             &QWebSocket::errorOccurred),
                         &loop, [&](QAbstractSocket::SocketError errorCode) {
                             failed = true;
                             failReason = socket.errorString();
                             SPDLOG_DEBUG("edge-tts: socket error {}: {}",
                                          static_cast<int>(errorCode),
                                          socket.errorString());
                             loop.quit();
                         });

        QNetworkRequest request(QUrl(
            QStringLiteral("%1&ConnectionId=%2&Sec-MS-GEC=%3"
                           "&Sec-MS-GEC-Version=%4")
                .arg(QLatin1String(kWssBaseUrl), randomHexId(),
                     generateSecMsGec(), QLatin1String(kSecMsGecVersion))));
        request.setRawHeader("User-Agent", QByteArray(kChromiumUserAgent));
        request.setRawHeader("Accept-Encoding",
                             QByteArrayLiteral("gzip, deflate, br, zstd"));
        request.setRawHeader("Accept-Language",
                             QByteArrayLiteral("en-US,en;q=0.9"));
        request.setRawHeader("Pragma", QByteArrayLiteral("no-cache"));
        request.setRawHeader("Cache-Control", QByteArrayLiteral("no-cache"));
        request.setRawHeader(
            "Cookie", "muid=" + randomHexId().toLatin1().toUpper() + ";");
        socket.open(request);

        timeout.start();
        loop.exec();

        socket.close();

        if (failed) {
            result.error =
                QStringLiteral("edge-tts failed: %1").arg(failReason);
            return result;
        }
        if (!turnEnded) {
            result.error = QStringLiteral("edge-tts timed out waiting for "
                                          "speech data.");
            return result;
        }
        if (chunkMp3.isEmpty()) {
            result.error = QStringLiteral("edge-tts returned no audio.");
            return result;
        }
        QString decodeError;
        const QByteArray chunkPcm = decodeMp3ToPcm24k(chunkMp3, &decodeError);
        if (chunkPcm.isEmpty()) {
            result.error = QStringLiteral("edge-tts audio decode failed: %1")
                               .arg(decodeError);
            return result;
        }
        result.pcm24k.append(chunkPcm);
    }

    SPDLOG_INFO("edge-tts: synthesized {} chars -> {} bytes PCM", text.size(),
                result.pcm24k.size());
    return result;
}

} // namespace talkinput
