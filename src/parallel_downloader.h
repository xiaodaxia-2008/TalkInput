#pragma once

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QUrl>
#include <memory>
#include <vector>

namespace talkinput
{

class ParallelDownloader : public QObject
{
    Q_OBJECT

public:
    explicit ParallelDownloader(QNetworkAccessManager *network,
                                int numChunks = 4,
                                QObject *parent = nullptr);
    ~ParallelDownloader() override;

    void start(const QUrl &url, const QString &destPath);
    void cancel();
    bool isRunning() const { return m_running; }
    qint64 receivedBytes() const { return m_receivedBytes; }
    qint64 totalBytes() const { return m_totalSize; }

signals:
    void downloadProgress(qint64 received, qint64 total);
    void finished(bool success, const QString &error);

private:
    void startHeadRequest();
    void onHeadFinished();
    void startChunkedDownload(qint64 resumeOffset);
    void startSingleDownload(qint64 resumeOffset);
    void onChunkReadyRead(int chunkIndex);
    void onChunkFinished(int chunkIndex);
    void onChunkError(int chunkIndex, QNetworkReply::NetworkError error);
    void onSingleReadyRead();
    void onSingleProgress(qint64 received, qint64 total);
    void onSingleFinished();
    void onSingleError(QNetworkReply::NetworkError error);
    void emitFinished(bool success, const QString &error);
    void abortAll();

    QNetworkAccessManager *m_network;
    int m_numChunks;
    QUrl m_url;
    QString m_destPath;
    QString m_tempPath;

    struct Chunk
    {
        QNetworkReply *reply = nullptr;
        qint64 startByte = 0;
        qint64 endByte = 0;
        qint64 written = 0;
        bool finished = false;
    };
    std::vector<Chunk> m_chunks;

    qint64 m_totalSize = -1;
    qint64 m_receivedBytes = 0;
    bool m_running = false;
    std::unique_ptr<QFile> m_file;
    QNetworkReply *m_headReply = nullptr;
    QNetworkReply *m_singleReply = nullptr;
};

} // namespace talkinput
