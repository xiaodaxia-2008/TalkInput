#include "speech_api_server.h"

#include "app_config.h"
#include "audio_utils.h"
#include "logging.h"
#include "tts/edge_tts_engine.h"
#include "tts/melo_tts_engine.h"
#include "tts/tts_audio.h"
#include "tts_engine.h"
#include "voice_input_controller.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <optional>
#include <vector>

namespace talkinput
{

namespace
{

constexpr qint64 MaxRequestBodyBytes = 64LL * 1024 * 1024;
constexpr qint64 MaxHeaderBytes = 16 * 1024;
constexpr int ConnectionTimeoutMs = 30000;
constexpr int TranscriptionTimeoutMs = 300000;

QString statusText(int code)
{
    switch (code) {
    case 200:
        return QStringLiteral("OK");
    case 204:
        return QStringLiteral("No Content");
    case 400:
        return QStringLiteral("Bad Request");
    case 401:
        return QStringLiteral("Unauthorized");
    case 404:
        return QStringLiteral("Not Found");
    case 405:
        return QStringLiteral("Method Not Allowed");
    case 413:
        return QStringLiteral("Payload Too Large");
    case 500:
        return QStringLiteral("Internal Server Error");
    case 501:
        return QStringLiteral("Not Implemented");
    case 503:
        return QStringLiteral("Service Unavailable");
    default:
        return QStringLiteral("Unknown");
    }
}

QString jsonEscape(const QString &text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'"':
            out += QLatin1String("\\\"");
            break;
        case u'\\':
            out += QLatin1String("\\\\");
            break;
        case u'\n':
            out += QLatin1String("\\n");
            break;
        case u'\r':
            out += QLatin1String("\\r");
            break;
        case u'\t':
            out += QLatin1String("\\t");
            break;
        default:
            if (c.unicode() < 0x20) {
                out += QStringLiteral("\\u%1").arg(c.unicode(), 4, 16,
                                                   QLatin1Char('0'));
            }
            else {
                out += c;
            }
            break;
        }
    }
    return out;
}

QByteArray buildResponse(int code, const QByteArray &body,
                         const char *contentType)
{
    const QByteArray status = statusText(code).toLatin1();
    const QByteArray head =
        "HTTP/1.1 " + QByteArray::number(code) + " " + status +
        "\r\n"
        "Content-Type: " +
        contentType +
        "\r\n"
        "Content-Length: " +
        QByteArray::number(body.size()) +
        "\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization, "
        "Content-Length\r\n"
        "Connection: close\r\n"
        "\r\n";
    return head + body;
}

struct MultipartField
{
    QString name;
    QString fileName;
    QString contentType;
    QByteArray data;
};

std::optional<std::vector<MultipartField>>
parseMultipart(const QByteArray &body, const QByteArray &boundary)
{
    if (boundary.isEmpty()) {
        return std::nullopt;
    }

    const QByteArray delim = "--" + boundary;
    QList<QByteArray> chunks;
    {
        qsizetype start = 0;
        qsizetype idx;
        while ((idx = body.indexOf(delim, start)) >= 0) {
            chunks.append(body.mid(start, idx - start));
            start = idx + delim.size();
        }
        chunks.append(body.mid(start));
    }

    std::vector<MultipartField> fields;
    for (qsizetype i = 1; i < chunks.size(); ++i) {
        QByteArray chunk = chunks[i];

        // Strip the CRLF that terminates the previous boundary.
        if (chunk.startsWith("\r\n")) {
            chunk.remove(0, 2);
        }
        else if (chunk.startsWith("\n")) {
            chunk.remove(0, 1);
        }

        if (chunk.startsWith("--")) {
            break; // closing delimiter
        }

        const qsizetype headerEnd = chunk.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return std::nullopt;
        }

        const QByteArray headers = chunk.left(headerEnd);
        QByteArray data = chunk.mid(headerEnd + 4);

        // Strip the CRLF that belongs to the following boundary.
        if (data.endsWith("\r\n")) {
            data.chop(2);
        }
        else if (data.endsWith("\n")) {
            data.chop(1);
        }

        MultipartField field;
        const QList<QByteArray> headerLines = headers.split('\n');
        for (const QByteArray &line : headerLines) {
            const QByteArray trimmed = line.trimmed();
            const qsizetype colon = trimmed.indexOf(':');
            if (colon < 0) {
                continue;
            }
            const QByteArray key = trimmed.left(colon).toLower();
            const QByteArray value = trimmed.mid(colon + 1).trimmed();
            if (key == "content-disposition") {
                const QString disposition = QString::fromLatin1(value);
                const QRegularExpression nameRe(
                    QStringLiteral("name=\"([^\"]*)\""));
                const QRegularExpression fileRe(
                    QStringLiteral("filename=\"([^\"]*)\""));
                const auto nameMatch = nameRe.match(disposition);
                if (nameMatch.hasMatch()) {
                    field.name = nameMatch.captured(1);
                }
                const auto fileMatch = fileRe.match(disposition);
                if (fileMatch.hasMatch()) {
                    field.fileName = fileMatch.captured(1);
                }
            }
            else if (key == "content-type") {
                field.contentType = QString::fromLatin1(value);
            }
        }
        field.data = std::move(data);
        fields.push_back(std::move(field));
    }

    return fields;
}

QString safeAudioSuffix(const QString &fileName)
{
    QString suffix = QFileInfo(fileName).suffix().toLower();
    suffix = suffix.left(10);
    QString clean;
    clean.reserve(suffix.size());
    for (const QChar c : suffix) {
        if (c.isLetterOrNumber()) {
            clean.append(c);
        }
    }
    return clean.isEmpty() ? QStringLiteral("audio") : clean;
}

QString errorTypeForCode(int code)
{
    if (code == 401) {
        return QStringLiteral("authentication_error");
    }
    return QStringLiteral("invalid_request_error");
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// SpeechApiServer (main-thread facade)
// ─────────────────────────────────────────────────────────────────────────

namespace
{

static SpeechApiServer *s_instance = nullptr;

} // namespace

class SpeechApiServer::Core final : public QObject
{
    Q_OBJECT

public:
    struct ConnectionState
    {
        QTcpSocket *socket = nullptr;
        QByteArray buffer;
        qsizetype headerEnd = 0;
        qsizetype bodyParsePos = 0;
        qint64 contentLength = 0;
        qint64 chunkRemaining = 0;
        QString method;
        QString path;
        QMap<QString, QString> headers;
        QByteArray body;
        bool headersParsed = false;
        bool chunked = false;
        bool readingChunkData = false;
        bool complete = false;
    };

    explicit Core(QObject *parent = nullptr) : QObject(parent)
    {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this,
                &Core::onNewConnection);
    }

    void setTranscriber(ApiTranscriber transcriber)
    {
        m_transcriber = std::move(transcriber);
    }

signals:
    void listeningChanged(bool listening);
    void serverStarted(quint16 port);
    void errorOccurred(const QString &message);
    void transcriptionCompleted(const QString &text);

public slots:

    void applySettings()
    {
        const auto &settings = appConfig().settings;
        const bool enabled = settings.apiServerEnabled;
        const QString host =
            QString::fromStdString(settings.apiServerHost).trimmed();
        const quint16 port =
            static_cast<quint16>(std::clamp(settings.apiServerPort, 0, 65535));
        const QString apiKey = QString::fromStdString(settings.apiServerApiKey);

        const bool isListening = m_server->isListening();
        if (enabled == m_enabled && host == m_host && port == m_port &&
            apiKey == m_apiKey && isListening == enabled)
        {
            return;
        }

        m_enabled = enabled;
        m_host = host;
        m_port = port;
        m_apiKey = apiKey;

        if (isListening) {
            m_server->close();
            emit listeningChanged(false);
        }

        if (!enabled) {
            SPDLOG_INFO("API server: disabled");
            return;
        }

        QHostAddress address;
        if (host.isEmpty() || host == QStringLiteral("0.0.0.0")) {
            address = QHostAddress::AnyIPv4;
        }
        else if (host == QStringLiteral("::")) {
            address = QHostAddress::AnyIPv6;
        }
        else {
            address = QHostAddress(host);
            if (address.isNull()) {
                address = QHostAddress::LocalHost;
            }
        }

        if (!m_server->listen(address, port)) {
            SPDLOG_ERROR("API server: listen failed on {}:{} — {}", m_host,
                         port, m_server->errorString());
            emit errorOccurred(QStringLiteral("Failed to listen on %1:%2: %3")
                                   .arg(m_host)
                                   .arg(port)
                                   .arg(m_server->errorString()));
            emit listeningChanged(false);
            return;
        }

        emit serverStarted(m_server->serverPort());
        emit listeningChanged(true);
        SPDLOG_INFO("API server: listening on http://{}:{} "
                    "(OpenAI-compatible /v1/audio/transcriptions)",
                    m_host, m_server->serverPort());
    }

    void shutdown()
    {
        m_ttsEngine.reset();
        if (m_server->isListening()) {
            m_server->close();
        }
        for (QTcpSocket *socket : m_pending) {
            socket->abort();
        }
        m_pending.clear();
        for (QTcpSocket *socket : m_connections.keys()) {
            socket->abort();
        }
        m_connections.clear();
        m_active = nullptr;
        emit listeningChanged(false);
    }

private:
    void onNewConnection()
    {
        while (m_server->hasPendingConnections()) {
            auto *socket = m_server->nextPendingConnection();
            socket->setParent(this);
            ConnectionState state;
            state.socket = socket;
            m_connections.insert(socket, state);
            connect(socket, &QTcpSocket::readyRead, this, &Core::onReadyRead);
            connect(socket, &QTcpSocket::disconnected, this,
                    &Core::onDisconnected);
            auto *timer = new QTimer(socket);
            timer->setSingleShot(true);
            timer->setInterval(ConnectionTimeoutMs);
            connect(timer, &QTimer::timeout, socket, &QTcpSocket::abort);
            timer->start();
        }
    }

    void onReadyRead()
    {
        auto *socket = qobject_cast<QTcpSocket *>(sender());
        if (!socket) {
            return;
        }
        auto it = m_connections.find(socket);
        if (it == m_connections.end()) {
            return;
        }
        ConnectionState &state = it.value();

        state.buffer.append(socket->readAll());

        if (!state.headersParsed) {
            const qsizetype end = state.buffer.indexOf("\r\n\r\n");
            if (end < 0) {
                if (state.buffer.size() > MaxHeaderBytes) {
                    respondErrorAndClose(socket, 413,
                                         QStringLiteral("Headers too large"));
                }
                return;
            }

            const QList<QByteArray> lines = state.buffer.left(end).split('\n');
            const QList<QByteArray> requestLine =
                lines.isEmpty() ? QList<QByteArray>()
                                : lines.first().trimmed().split(' ');
            if (requestLine.size() < 2) {
                respondErrorAndClose(socket, 400,
                                     QStringLiteral("Malformed request line"));
                return;
            }
            state.method = QString::fromLatin1(requestLine.at(0));
            state.path = QString::fromLatin1(requestLine.at(1));

            for (qsizetype i = 1; i < lines.size(); ++i) {
                const QByteArray trimmed = lines.at(i).trimmed();
                const qsizetype colon = trimmed.indexOf(':');
                if (colon <= 0) {
                    continue;
                }
                const QString key =
                    QString::fromLatin1(trimmed.left(colon).toLower());
                const QString value =
                    QString::fromLatin1(trimmed.mid(colon + 1).trimmed());
                state.headers.insert(key, value);
            }

            state.contentLength = state.headers
                                      .value(QStringLiteral("content-length"),
                                             QStringLiteral("0"))
                                      .toLongLong();
            if (state.contentLength > MaxRequestBodyBytes) {
                respondErrorAndClose(socket, 413,
                                     QStringLiteral("Request body too large"));
                return;
            }
            const QString transferEncoding =
                state.headers.value(QStringLiteral("transfer-encoding"))
                    .trimmed()
                    .toLower();
            if (!transferEncoding.isEmpty()) {
                if (transferEncoding != QStringLiteral("chunked")) {
                    respondErrorAndClose(
                        socket, 501,
                        QStringLiteral("Unsupported transfer encoding"));
                    return;
                }
                state.chunked = true;
            }
            if (state.headers.value(QStringLiteral("expect"))
                    .contains(QStringLiteral("100-continue"),
                              Qt::CaseInsensitive))
            {
                socket->write("HTTP/1.1 100 Continue\r\n\r\n");
            }
            state.headerEnd = end + 4;
            state.bodyParsePos = state.headerEnd;
            state.headersParsed = true;
        }

        if (state.chunked) {
            if (!consumeChunkedBody(state)) {
                return;
            }
            if (!state.complete) {
                return; // an error was already answered and the socket closed
            }
        }
        else {
            if (state.buffer.size() < state.headerEnd + state.contentLength) {
                return;
            }
            state.body = state.buffer.mid(state.headerEnd, state.contentLength);
            state.complete = true;
        }

        if (auto *timer = socket->findChild<QTimer *>()) {
            timer->stop();
        }

        if (m_active) {
            m_pending.append(socket);
        }
        else {
            m_active = socket;
            processRequest(socket);
        }
    }

    bool consumeChunkedBody(ConnectionState &state)
    {
        qsizetype &pos = state.bodyParsePos;
        while (true) {
            if (!state.readingChunkData) {
                const qsizetype lineEnd = state.buffer.indexOf("\r\n", pos);
                if (lineEnd < 0) {
                    return false;
                }
                const QByteArray line = state.buffer.mid(pos, lineEnd - pos);
                const qsizetype semi = line.indexOf(';');
                bool ok = false;
                const qint64 chunkSize = (semi < 0 ? line : line.left(semi))
                                             .trimmed()
                                             .toLongLong(&ok, 16);
                if (!ok || chunkSize < 0) {
                    respondErrorAndClose(state.socket, 400,
                                         QStringLiteral("Invalid chunk size"));
                    return true;
                }
                pos = lineEnd + 2;
                if (chunkSize == 0) {
                    // trailer-part is zero or more header lines followed by a
                    // final CRLF. The common case (no trailers) is a bare CRLF
                    // right after "0\r\n".
                    if (state.buffer.size() < pos + 2) {
                        return false;
                    }
                    if (state.buffer.at(pos) == '\r' &&
                        state.buffer.at(pos + 1) == '\n')
                    {
                        state.complete = true;
                        return true;
                    }
                    const qsizetype trailerEnd =
                        state.buffer.indexOf("\r\n\r\n", pos);
                    if (trailerEnd < 0) {
                        return false;
                    }
                    state.complete = true;
                    return true;
                }
                if (chunkSize > MaxRequestBodyBytes) {
                    respondErrorAndClose(
                        state.socket, 413,
                        QStringLiteral("Request body too large"));
                    return true;
                }
                state.chunkRemaining = chunkSize;
                state.readingChunkData = true;
            }

            if (state.buffer.size() < pos + state.chunkRemaining + 2) {
                return false;
            }
            if (state.body.size() + state.chunkRemaining > MaxRequestBodyBytes)
            {
                respondErrorAndClose(state.socket, 413,
                                     QStringLiteral("Request body too large"));
                return true;
            }
            state.body.append(state.buffer.mid(pos, state.chunkRemaining));
            pos += state.chunkRemaining;
            if (state.buffer.at(pos) != '\r' ||
                state.buffer.at(pos + 1) != '\n')
            {
                respondErrorAndClose(state.socket, 400,
                                     QStringLiteral("Malformed chunk data"));
                return true;
            }
            pos += 2;
            state.readingChunkData = false;
            state.chunkRemaining = 0;
        }
    }

    void onDisconnected()
    {
        auto *socket = qobject_cast<QTcpSocket *>(sender());
        if (!socket) {
            return;
        }
        m_connections.remove(socket);
        m_pending.removeAll(socket);
        if (m_active == socket) {
            m_active = nullptr;
            socket->deleteLater();
            processNext();
        }
        else {
            socket->deleteLater();
        }
    }

    void processNext()
    {
        while (!m_active && !m_pending.isEmpty()) {
            QTcpSocket *socket = m_pending.takeFirst();
            auto it = m_connections.find(socket);
            if (it != m_connections.end() && it->complete) {
                m_active = socket;
                processRequest(socket);
                return;
            }
        }
    }

    void processRequest(QTcpSocket *socket)
    {
        auto it = m_connections.find(socket);
        if (it == m_connections.end()) {
            onDisconnectedFor(socket);
            return;
        }
        const ConnectionState &state = it.value();

        const QString method = state.method.toUpper();
        const QString path = state.path.section(QLatin1Char('?'), 0, 0);

        if (method == QStringLiteral("OPTIONS")) {
            respond(socket, 204, {}, "text/plain; charset=utf-8");
            return;
        }
        if (path == QStringLiteral("/") || path == QStringLiteral("/health") ||
            path == QStringLiteral("/healthz"))
        {
            handleHealth(socket);
            return;
        }
        if (path == QStringLiteral("/v1/models")) {
            if (method != QStringLiteral("GET")) {
                respondError(socket, 405, QStringLiteral("Method not allowed"));
                return;
            }
            if (!authorize(state)) {
                return;
            }
            handleModels(socket);
            return;
        }
        if (path == QStringLiteral("/v1/audio/transcriptions")) {
            if (method != QStringLiteral("POST")) {
                respondError(socket, 405, QStringLiteral("Method not allowed"));
                return;
            }
            if (!authorize(state)) {
                return;
            }
            handleTranscription(state);
            return;
        }
        if (path == QStringLiteral("/v1/audio/speech")) {
            if (method != QStringLiteral("POST")) {
                respondError(socket, 405, QStringLiteral("Method not allowed"));
                return;
            }
            if (!authorize(state)) {
                return;
            }
            handleSpeech(state);
            return;
        }

        respondError(socket, 404, QStringLiteral("Not found: %1").arg(path));
    }

    void handleHealth(QTcpSocket *socket)
    {
        const QString model =
            QString::fromStdString(appConfig().settings.asrProviderId);
        const QByteArray body =
            QStringLiteral("{\"status\":\"ok\",\"service\":\"TalkInput\","
                           "\"model\":\"%1\"}")
                .arg(jsonEscape(model))
                .toUtf8();
        respond(socket, 200, body, "application/json; charset=utf-8");
    }

    void handleModels(QTcpSocket *socket)
    {
        // Only the model currently loaded into the shared recognizer is
        // available; any other model id in a request is ignored.
        const std::string &loadedId = appConfig().settings.asrProviderId;
        const auto &presets = appConfig().asrPresets;
        const auto it = presets.find(loadedId);
        QString item;
        if (it != presets.end()) {
            item =
                QStringLiteral("{\"id\":\"%1\",\"object\":\"model\","
                               "\"owned_by\":\"talkinput\",\"name\":\"%2\"}")
                    .arg(jsonEscape(QString::fromStdString(it->first)),
                         jsonEscape(QString::fromStdString(it->second.name)));
        }
        const QByteArray body =
            QStringLiteral("{\"object\":\"list\",\"data\":[%1]}")
                .arg(item)
                .toUtf8();
        respond(socket, 200, body, "application/json; charset=utf-8");
    }

    void handleTranscription(const ConnectionState &state)
    {
        const QString contentType =
            state.headers.value(QStringLiteral("content-type"));
        const qsizetype boundaryPos = contentType.indexOf(
            QStringLiteral("boundary="), 0, Qt::CaseInsensitive);
        if (boundaryPos < 0) {
            respondError(state.socket, 400,
                         QStringLiteral("Expected multipart/form-data"));
            return;
        }
        const QByteArray boundary =
            contentType.mid(boundaryPos + 9).trimmed().toLatin1();

        auto fields = parseMultipart(state.body, boundary);
        if (!fields) {
            respondError(state.socket, 400,
                         QStringLiteral("Invalid multipart body"));
            return;
        }

        const MultipartField *file = nullptr;
        QString language;
        QString responseFormat = QStringLiteral("json");
        QString prompt;
        for (const auto &field : *fields) {
            if (field.name == QStringLiteral("file")) {
                file = &field;
            }
            else if (field.name == QStringLiteral("language")) {
                language = QString::fromUtf8(field.data).trimmed();
            }
            else if (field.name == QStringLiteral("response_format")) {
                responseFormat = QString::fromUtf8(field.data).trimmed();
            }
            else if (field.name == QStringLiteral("prompt")) {
                prompt = QString::fromUtf8(field.data).trimmed();
            }
        }
        if (!file) {
            respondError(state.socket, 400,
                         QStringLiteral("Missing 'file' field"));
            return;
        }
        if (file->data.isEmpty()) {
            respondError(state.socket, 400, QStringLiteral("Empty audio file"));
            return;
        }
        if (language.isEmpty()) {
            const auto &presets = appConfig().asrPresets;
            const auto it = presets.find(appConfig().settings.asrProviderId);
            if (it != presets.end()) {
                language = QString::fromStdString(it->second.languages);
            }
        }

        QString error;
        TranscriptionResult result;
        if (m_transcriber) {
            result = m_transcriber(file->data, file->fileName, &error);
        }
        else {
            result = transcribeFile(file->data, file->fileName, &error);
        }
        if (!error.isEmpty()) {
            respondError(state.socket, 500, error);
            return;
        }

        emit transcriptionCompleted(result.text);

        if (responseFormat == QStringLiteral("text")) {
            respond(state.socket, 200, result.text.toUtf8(),
                    "text/plain; charset=utf-8");
            return;
        }
        if (responseFormat == QStringLiteral("verbose_json")) {
            const QByteArray body =
                QStringLiteral(
                    "{\"task\":\"transcribe\",\"language\":\"%1\","
                    "\"duration\":%2,\"text\":\"%3\",\"segments\":[]}")
                    .arg(jsonEscape(result.language.isEmpty()
                                        ? language
                                        : result.language),
                         QString::number(result.duration, 'g', 6),
                         jsonEscape(result.text))
                    .toUtf8();
            respond(state.socket, 200, body, "application/json; charset=utf-8");
            return;
        }

        const QByteArray body = QStringLiteral("{\"text\":\"%1\"}")
                                    .arg(jsonEscape(result.text))
                                    .toUtf8();
        respond(state.socket, 200, body, "application/json; charset=utf-8");
    }

    void handleSpeech(const ConnectionState &state)
    {
        QJsonParseError parseError;
        const QJsonDocument doc =
            QJsonDocument::fromJson(state.body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            respondError(state.socket, 400,
                         QStringLiteral("Invalid JSON request body"));
            return;
        }
        const QJsonObject obj = doc.object();

        const QString input = obj.value(QLatin1String("input")).toString().trimmed();
        if (input.isEmpty()) {
            respondError(state.socket, 400,
                         QStringLiteral("The 'input' field is required."));
            return;
        }
        const QString voice =
            obj.value(QLatin1String("voice")).toString().trimmed();
        const QString responseFormat =
            obj.value(QLatin1String("response_format")).toString().trimmed();
        double speed = 1.0;
        if (obj.contains(QLatin1String("speed"))) {
            speed = obj.value(QLatin1String("speed")).toDouble(1.0);
        }

        const QString provider =
            QString::fromStdString(appConfig().settings.ttsProvider);
        TtsEngine *engine = getTtsEngine(provider);
        if (!engine) {
            respondError(state.socket, 503,
                         QStringLiteral("TTS provider '%1' is unavailable. "
                                        "Check the TTS settings or install the "
                                        "offline model.")
                             .arg(provider));
            return;
        }

        const TtsSynthesisResult result = engine->synthesize(input, voice, speed);
        if (!result.ok()) {
            respondError(state.socket, 500, result.error);
            return;
        }

        const QByteArray format = responseFormat.toLower().toUtf8();
        if (format.isEmpty() || format == "pcm") {
            respond(state.socket, 200, result.pcm24k,
                    "audio/pcm; rate=24000; bits=16; channels=1");
            return;
        }
        if (format == "wav") {
            respond(state.socket, 200, pcm16ToWav(result.pcm24k, 24000),
                    "audio/wav");
            return;
        }
        if (format == "mp3") {
            QString mp3Error;
            const QByteArray mp3 =
                pcm16ToMp3(result.pcm24k, 24000, &mp3Error);
            if (mp3.isEmpty()) {
                respondError(state.socket, 500,
                             QStringLiteral("MP3 encoding failed: %1")
                                 .arg(mp3Error));
                return;
            }
            respond(state.socket, 200, mp3, "audio/mpeg");
            return;
        }
        respondError(state.socket, 400,
                     QStringLiteral("Unsupported response_format '%1'.")
                         .arg(responseFormat));
    }

    TtsEngine *getTtsEngine(const QString &provider)
    {
        if (m_ttsEngine && m_ttsProvider == provider) {
            return m_ttsEngine.get();
        }
        m_ttsEngine.reset();
        m_ttsProvider = provider;

        if (provider == QStringLiteral("melo")) {
            if (!MeloTtsEngine::isModelInstalled()) {
                SPDLOG_WARN("MeloTTS: model not installed");
                return nullptr;
            }
            m_ttsEngine = std::make_unique<MeloTtsEngine>();
        }
        else if (provider == QStringLiteral("edge")) {
            m_ttsEngine = std::make_unique<EdgeTtsEngine>();
        }
        else {
            SPDLOG_WARN("API server: unknown TTS provider '{}'",
                        provider.toStdString());
            return nullptr;
        }
        return m_ttsEngine.get();
    }

    bool authorize(const ConnectionState &state)
    {
        if (m_apiKey.isEmpty()) {
            return true;
        }
        const QString authorization =
            state.headers.value(QStringLiteral("authorization"));
        const QString bearer = QStringLiteral("Bearer %1").arg(m_apiKey);
        if (authorization.compare(bearer, Qt::CaseInsensitive) != 0) {
            respondError(state.socket, 401, QStringLiteral("Invalid API key"));
            return false;
        }
        return true;
    }

    TranscriptionResult transcribeShared(const QByteArray &pcm16,
                                         int sampleRate, int channels,
                                         QString *error)
    {
        auto *controller = VoiceInputController::instance();
        if (!controller) {
            *error = QStringLiteral("Voice input controller unavailable.");
            return {};
        }

        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        timeout.setInterval(TranscriptionTimeoutMs);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(qApp, &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit);
        timeout.start();

        TranscriptionResult result;
        QString apiError;
        QMetaObject::invokeMethod(
            controller,
            [controller, &loop, &result, &apiError, pcm16, sampleRate,
             channels]() {
                controller->submitApiTranscription(
                    pcm16, sampleRate, channels,
                    [&loop, &result,
                     &apiError](const ApiTranscriptionResult &r) {
                        // Delivered on the GUI thread; marshal back to the
                        // API server thread before touching shared state.
                        QMetaObject::invokeMethod(
                            &loop,
                            [&loop, &result, &apiError, r]() {
                                result.text = r.text;
                                result.duration = r.duration;
                                apiError = r.error;
                                loop.quit();
                            },
                            Qt::QueuedConnection);
                    });
            },
            Qt::QueuedConnection);

        loop.exec();

        if (!apiError.isEmpty()) {
            *error = apiError;
            return {};
        }
        return result;
    }

    TranscriptionResult transcribeFile(const QByteArray &audioData,
                                       const QString &fileName, QString *error)
    {
        QTemporaryFile tempFile;
        tempFile.setFileTemplate(QDir::tempPath() + "/talkinput-api-XXXXXX." +
                                 safeAudioSuffix(fileName));
        if (!tempFile.open()) {
            *error = QStringLiteral("Failed to create temporary audio file");
            return {};
        }
        tempFile.write(audioData);
        tempFile.flush();

        auto decoded = decodeAudioFileToPcm16(tempFile.fileName());
        if (!decoded) {
            *error = QStringLiteral("Failed to decode audio: %1")
                         .arg(decoded.error());
            return {};
        }

        TranscriptionResult result = transcribeShared(
            decoded->pcm16, decoded->sampleRate, decoded->channels, error);
        if (!error->isEmpty()) {
            return {};
        }

        const auto &presets = appConfig().asrPresets;
        const auto it = presets.find(appConfig().settings.asrProviderId);
        if (it != presets.end()) {
            result.language = QString::fromStdString(it->second.languages);
        }
        return result;
    }

    void respond(QTcpSocket *socket, int code, const QByteArray &body,
                 const char *contentType)
    {
        socket->write(buildResponse(code, body, contentType));
        socket->flush();
        socket->disconnectFromHost();
    }

    void respondError(QTcpSocket *socket, int code, const QString &message)
    {
        const QByteArray body =
            QStringLiteral("{\"error\":{\"message\":\"%1\",\"type\":\"%2\","
                           "\"param\":null,\"code\":null}}")
                .arg(jsonEscape(message), jsonEscape(errorTypeForCode(code)))
                .toUtf8();
        respond(socket, code, body, "application/json; charset=utf-8");
    }

    void respondErrorAndClose(QTcpSocket *socket, int code,
                              const QString &message)
    {
        respondError(socket, code, message);
        m_pending.removeAll(socket);
        m_connections.remove(socket);
        socket->deleteLater();
    }

    void onDisconnectedFor(QTcpSocket *socket)
    {
        m_connections.remove(socket);
        m_pending.removeAll(socket);
        if (m_active == socket) {
            m_active = nullptr;
            socket->deleteLater();
            processNext();
        }
        else {
            socket->deleteLater();
        }
    }

    QTcpServer *m_server = nullptr;
    QList<QTcpSocket *> m_pending;
    QTcpSocket *m_active = nullptr;
    QHash<QTcpSocket *, ConnectionState> m_connections;
    ApiTranscriber m_transcriber;
    QString m_ttsProvider;
    std::unique_ptr<TtsEngine> m_ttsEngine;
    bool m_enabled = false;
    QString m_host;
    quint16 m_port = 0;
    QString m_apiKey;
};

SpeechApiServer::SpeechApiServer(QObject *parent) : QObject(parent)
{
    s_instance = this;
    m_thread = std::make_unique<QThread>();
    m_thread->setObjectName(QStringLiteral("TalkInputApiServer"));
    m_core = new Core();
    m_core->moveToThread(m_thread.get());
    connect(m_core, &Core::listeningChanged, this,
            &SpeechApiServer::listeningChanged);
    connect(m_core, &Core::serverStarted, this,
            &SpeechApiServer::serverStarted);
    connect(m_core, &Core::errorOccurred, this,
            &SpeechApiServer::errorOccurred);
    connect(m_core, &Core::transcriptionCompleted, this,
            &SpeechApiServer::transcriptionCompleted);
    connect(m_thread.get(), &QThread::finished, m_core, &QObject::deleteLater);
    m_thread->start();
}

SpeechApiServer::~SpeechApiServer()
{
    shutdown();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

SpeechApiServer *SpeechApiServer::instance()
{
    return s_instance;
}

void SpeechApiServer::setTranscriber(ApiTranscriber transcriber)
{
    m_transcriber = std::move(transcriber);
}

void SpeechApiServer::applySettings()
{
    ApiTranscriber transcriber = m_transcriber;
    Core *core = m_core;
    QMetaObject::invokeMethod(
        core,
        [core, transcriber = std::move(transcriber)]() mutable {
            if (transcriber) {
                core->setTranscriber(std::move(transcriber));
            }
            core->applySettings();
        },
        Qt::QueuedConnection);
}

void SpeechApiServer::shutdown()
{
    if (!m_thread || !m_core) {
        return;
    }
    if (m_thread->isRunning()) {
        QMetaObject::invokeMethod(m_core, &Core::shutdown,
                                  Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait(3000);
    }
    m_core = nullptr;
    m_thread.reset();
}

} // namespace talkinput

#include "speech_api_server.moc"
