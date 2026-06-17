#pragma once

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QTimer>
#include <functional>
#include <memory>

class QNetworkReply;

namespace talkinput
{

class LlmPostProcessor final : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const QString &)>;

    explicit LlmPostProcessor(QObject *parent = nullptr);
    ~LlmPostProcessor() override;

    bool isEnabled() const;
    void postProcess(const QString &text, QObject *receiver, Callback callback);

signals:
    void statusMessage(const QString &message);

private:
    enum class DownloadKind
    {
        None,
        LlamaArchive,
        Model
    };

    struct PendingRequest
    {
        QString text;
        QPointer<QObject> receiver;
        Callback callback;
    };

    QString baseDir() const;
    QString llamaDir() const;
    QString modelDir() const;
    QString llamaArchivePath() const;
    QString modelPath() const;
    QString serverExecutablePath() const;

    void ensureReady();
    void beginDownload(DownloadKind kind, const QUrl &url, const QString &path);
    void onDownloadFinished(QNetworkReply *reply);
    bool extractLlamaArchive(QString *errorMessage);
    void startServer();
    void pollHealth();
    void sendCompletion(const PendingRequest &request);
    void drainQueue();
    void failPending(const QString &reason);
    static QString cleanupResponseText(const QString &text);

    QNetworkAccessManager m_network;
    QProcess m_server;
    QTimer m_healthTimer;
    QQueue<PendingRequest> m_pending;
    std::unique_ptr<QFile> m_downloadFile;
    QNetworkReply *m_activeDownload = nullptr;
    QString m_activeDownloadPath;
    DownloadKind m_downloadKind = DownloadKind::None;
    bool m_preparing = false;
    bool m_serverReady = false;
    int m_healthAttempts = 0;
};

} // namespace talkinput
