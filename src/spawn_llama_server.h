#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <expected>

namespace talkinput
{

class ParallelDownloader;

QString llamaServerExecutableName();
QUrl llamaServerArchiveUrl();

class LlamaServerManager : public QObject
{
    Q_OBJECT

public:
    explicit LlamaServerManager(QObject *parent = nullptr);
    ~LlamaServerManager() override;

    void start();
    void stop();
    bool isReady() const;

signals:
    void ready();
    void failed(const QString &reason);

private:
    enum class DownloadKind
    {
        None,
        LlamaArchive,
        Model
    };

    QString baseDir() const;
    QString llamaDir() const;
    QString modelDir() const;
    QString llamaArchivePath() const;
    QString modelPath() const;
    QString serverExecutablePath() const;

    void prepare();
    void beginDownload(DownloadKind kind, const QUrl &url, const QString &path);
    void onParallelDownloadFinished(bool ok, const QString &error);
    std::expected<void, QString> extractLlamaArchive();
    void startServer();
    void pollHealth();

    QNetworkAccessManager m_network;
    QProcess m_server;
    QTimer m_healthTimer;
    ParallelDownloader *m_downloader = nullptr;
    DownloadKind m_downloadKind = DownloadKind::None;
    bool m_ready = false;
    bool m_stopping = false;
    bool m_preparing = false;
    int m_healthAttempts = 0;
};

} // namespace talkinput
