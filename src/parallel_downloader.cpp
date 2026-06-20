#include "parallel_downloader.h"

#include <QFileInfo>

namespace talkinput
{

ParallelDownloader::ParallelDownloader(QNetworkAccessManager *network,
                                       int numChunks, QObject *parent)
    : QObject(parent), m_network(network), m_numChunks(
                                                std::max(1, numChunks))
{
}

ParallelDownloader::~ParallelDownloader()
{
    cancel();
}

void ParallelDownloader::start(const QUrl &url, const QString &destPath)
{
    if (m_running) {
        return;
    }

    m_url = url;
    m_destPath = destPath;
    m_tempPath = destPath + QStringLiteral(".part");
    m_totalSize = -1;
    m_receivedBytes = 0;
    m_running = true;

    startHeadRequest();
}

void ParallelDownloader::cancel()
{
    abortAll();
    if (m_headReply) {
        m_headReply->abort();
        m_headReply->deleteLater();
        m_headReply = nullptr;
    }
    if (m_singleReply) {
        m_singleReply->abort();
        m_singleReply->deleteLater();
        m_singleReply = nullptr;
    }
    for (auto &chunk : m_chunks) {
        if (chunk.reply) {
            chunk.reply->abort();
            chunk.reply->deleteLater();
            chunk.reply = nullptr;
        }
    }
    m_chunks.clear();
    m_file.reset();
    m_running = false;
}

void ParallelDownloader::startHeadRequest()
{
    QNetworkRequest request(m_url);
    m_headReply = m_network->head(request);

    connect(m_headReply, &QNetworkReply::finished, this,
            [this]() { onHeadFinished(); });
    connect(m_headReply, &QNetworkReply::errorOccurred, this,
            [this](QNetworkReply::NetworkError) {
                QString error = m_headReply->errorString();
                m_headReply->deleteLater();
                m_headReply = nullptr;
                emitFinished(false, error);
            });
}

void ParallelDownloader::onHeadFinished()
{
    if (!m_running || !m_headReply) {
        return;
    }

    if (m_headReply->error() != QNetworkReply::NoError) {
        QString err = m_headReply->errorString();
        m_headReply->deleteLater();
        m_headReply = nullptr;
        emitFinished(false, err);
        return;
    }

    const int statusCode =
        m_headReply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
            .toInt();
    if (statusCode == 302 || statusCode == 301) {
        const QUrl redirectUrl =
            m_headReply->attribute(
                           QNetworkRequest::RedirectionTargetAttribute)
                .toUrl();
        m_headReply->deleteLater();
        m_headReply = nullptr;
        if (redirectUrl.isValid()) {
            m_url = redirectUrl;
            startHeadRequest();
            return;
        }
    }

    bool rangeSupported = false;
    const QString acceptRanges =
        m_headReply->rawHeader("Accept-Ranges");
    if (acceptRanges.contains("bytes", Qt::CaseInsensitive)) {
        rangeSupported = true;
    } else if (statusCode == 206) {
        rangeSupported = true;
    }

    const qint64 contentLength =
        m_headReply->header(QNetworkRequest::ContentLengthHeader)
            .toLongLong();
    if (contentLength <= 0) {
        m_headReply->deleteLater();
        m_headReply = nullptr;
        emitFinished(false, QStringLiteral("Invalid content length"));
        return;
    }
    m_totalSize = contentLength;

    m_headReply->deleteLater();
    m_headReply = nullptr;

    qint64 resumeOffset = 0;
    if (QFileInfo::exists(m_tempPath)) {
        resumeOffset = QFileInfo(m_tempPath).size();
        if (resumeOffset >= m_totalSize) {
            QFile::remove(m_tempPath);
            resumeOffset = 0;
        }
    }

    if (rangeSupported && m_numChunks > 1) {
        startChunkedDownload(resumeOffset);
    } else {
        startSingleDownload(resumeOffset);
    }
}

void ParallelDownloader::startChunkedDownload(qint64 resumeOffset)
{
    m_file = std::make_unique<QFile>(m_tempPath);
    auto mode = (resumeOffset > 0) ? QIODevice::Append
                                   : QIODevice::WriteOnly;
    if (!m_file->open(mode)) {
        emitFinished(false, QStringLiteral("Cannot create download file"));
        return;
    }

    const qint64 remaining = m_totalSize - resumeOffset;
    const qint64 chunkSize = (remaining + m_numChunks - 1) / m_numChunks;
    m_chunks.reserve(m_numChunks);

    for (int i = 0; i < m_numChunks; ++i) {
        const qint64 start = resumeOffset + i * chunkSize;
        if (start >= m_totalSize) {
            break;
        }
        qint64 end = start + chunkSize - 1;
        if (end >= m_totalSize) {
            end = m_totalSize - 1;
        }

        Chunk chunk;
        chunk.startByte = start;
        chunk.endByte = end;

        QNetworkRequest request(m_url);
        request.setRawHeader(
            "Range",
            QStringLiteral("bytes=%1-%2")
                .arg(start)
                .arg(end)
                .toUtf8());

        chunk.reply = m_network->get(request);
        const int idx = static_cast<int>(m_chunks.size());

        connect(chunk.reply, &QNetworkReply::readyRead, this,
                [this, idx]() { onChunkReadyRead(idx); });
        connect(chunk.reply, &QNetworkReply::finished, this,
                [this, idx]() { onChunkFinished(idx); });
        connect(chunk.reply, &QNetworkReply::errorOccurred, this,
                [this, idx](QNetworkReply::NetworkError e) {
                    onChunkError(idx, e);
                });

        m_chunks.push_back(std::move(chunk));
    }

    if (m_chunks.empty()) {
        emitFinished(true, {});
    }
}

void ParallelDownloader::startSingleDownload(qint64 resumeOffset)
{
    m_file = std::make_unique<QFile>(m_tempPath);
    auto mode = (resumeOffset > 0) ? QIODevice::Append
                                   : QIODevice::WriteOnly;
    if (!m_file->open(mode)) {
        emitFinished(false, QStringLiteral("Cannot create download file"));
        return;
    }

    if (resumeOffset > 0) {
        m_receivedBytes = resumeOffset;
        emit downloadProgress(m_receivedBytes, m_totalSize);
    }

    QNetworkRequest request(m_url);
    if (resumeOffset > 0) {
        request.setRawHeader(
            "Range",
            QStringLiteral("bytes=%1-").arg(resumeOffset).toUtf8());
    }

    m_singleReply = m_network->get(request);

    connect(m_singleReply, &QNetworkReply::readyRead, this,
            [this]() { onSingleReadyRead(); });
    connect(m_singleReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 r, qint64 t) { onSingleProgress(r, t); });
    connect(m_singleReply, &QNetworkReply::finished, this,
            [this]() { onSingleFinished(); });
    connect(m_singleReply, &QNetworkReply::errorOccurred, this,
            [this](QNetworkReply::NetworkError e) {
                onSingleError(e);
            });
}

void ParallelDownloader::onChunkReadyRead(int chunkIndex)
{
    if (!m_running || chunkIndex < 0 ||
        chunkIndex >= static_cast<int>(m_chunks.size()))
    {
        return;
    }

    Chunk &chunk = m_chunks[chunkIndex];
    if (!chunk.reply || !m_file) {
        return;
    }

    const QByteArray data = chunk.reply->readAll();
    if (data.isEmpty()) {
        return;
    }

    m_file->seek(chunk.startByte + chunk.written);
    m_file->write(data);
    chunk.written += data.size();
    m_receivedBytes += data.size();

    emit downloadProgress(m_receivedBytes, m_totalSize);
}

void ParallelDownloader::onChunkFinished(int chunkIndex)
{
    if (!m_running || chunkIndex < 0 ||
        chunkIndex >= static_cast<int>(m_chunks.size()))
    {
        return;
    }

    Chunk &chunk = m_chunks[chunkIndex];
    if (!chunk.reply) {
        return;
    }

    if (chunk.reply->error() != QNetworkReply::NoError) {
        return;
    }

    chunk.finished = true;

    bool allDone = true;
    for (const auto &c : m_chunks) {
        if (!c.finished) {
            allDone = false;
            break;
        }
    }

    if (allDone) {
        m_file->close();
        m_file.reset();
        QFile::remove(m_destPath);
        QFile::rename(m_tempPath, m_destPath);

        for (auto &c : m_chunks) {
            if (c.reply) {
                c.reply->deleteLater();
                c.reply = nullptr;
            }
        }
        m_chunks.clear();
        m_running = false;
        emit finished(true, {});
    }
}

void ParallelDownloader::onChunkError(int chunkIndex,
                                      QNetworkReply::NetworkError /*error*/)
{
    if (!m_running) {
        return;
    }

    QString errorStr;
    if (chunkIndex >= 0 &&
        chunkIndex < static_cast<int>(m_chunks.size()) &&
        m_chunks[chunkIndex].reply)
    {
        errorStr = m_chunks[chunkIndex].reply->errorString();
    } else {
        errorStr = QStringLiteral("Unknown download error");
    }

    abortAll();
    QFile::remove(m_tempPath);
    m_running = false;
    emit finished(false, errorStr);
}

void ParallelDownloader::onSingleReadyRead()
{
    if (!m_running || !m_singleReply || !m_file) {
        return;
    }
    const QByteArray data = m_singleReply->readAll();
    if (data.isEmpty()) {
        return;
    }
    m_file->write(data);
    m_receivedBytes += data.size();
}

void ParallelDownloader::onSingleProgress(qint64 received, qint64 /*total*/)
{
    if (!m_running) {
        return;
    }
    emit downloadProgress(m_receivedBytes + received, m_totalSize);
}

void ParallelDownloader::onSingleFinished()
{
    if (!m_running || !m_singleReply || !m_file) {
        return;
    }

    if (m_singleReply->error() != QNetworkReply::NoError) {
        QString err = m_singleReply->errorString();
        m_singleReply->deleteLater();
        m_singleReply = nullptr;
        m_file->close();
        m_file.reset();
        QFile::remove(m_tempPath);
        m_running = false;
        emit finished(false, err);
        return;
    }

    m_file->write(m_singleReply->readAll());
    m_file->close();
    m_file.reset();

    m_singleReply->deleteLater();
    m_singleReply = nullptr;

    QFile::remove(m_destPath);
    if (!QFile::rename(m_tempPath, m_destPath)) {
        m_running = false;
        emit finished(false,
                      QStringLiteral("Failed to save download"));
        return;
    }

    m_running = false;
    emit finished(true, {});
}

void ParallelDownloader::onSingleError(QNetworkReply::NetworkError /*error*/)
{
    if (!m_running || !m_singleReply) {
        return;
    }
    QString err = m_singleReply->errorString();
    m_singleReply->abort();
    m_singleReply->deleteLater();
    m_singleReply = nullptr;
    m_file->close();
    m_file.reset();
    QFile::remove(m_tempPath);
    m_running = false;
    emit finished(false, err);
}

void ParallelDownloader::emitFinished(bool success, const QString &error)
{
    m_running = false;
    emit finished(success, error);
}

void ParallelDownloader::abortAll()
{
    for (auto &chunk : m_chunks) {
        if (chunk.reply) {
            chunk.reply->abort();
        }
    }
}

} // namespace talkinput
