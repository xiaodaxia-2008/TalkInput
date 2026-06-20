#pragma once

#include <QObject>
#include <QQueue>
#include <QString>

#include <memory>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace talkinput
{

class ModelDownloadManager final : public QObject
{
    Q_OBJECT

public:
    explicit ModelDownloadManager(QObject *parent = nullptr);
    ~ModelDownloadManager() override;

    bool startAsrModelDownload(const QString &modelPointer,
                               QString *errorMessage = nullptr);

signals:
    void downloadStarted(const QString &modelName);
    void extracting();
    void downloadFailed(const QString &modelName);
    void extractionFailed(const QString &error);
    void finished(const QString &requestedModelPointer);

private:
    bool enqueueAsrModelDownloads(const QString &modelPointer,
                                  QString *errorMessage);
    void startNextDownload();
    void onDownloadFinished();
    void clearActiveDownload();

    std::unique_ptr<QNetworkAccessManager> m_network;
    QNetworkReply *m_reply = nullptr;
    std::unique_ptr<QFile> m_file;
    QString m_archivePath;
    QString m_tempPath;
    QString m_activeModelPointer;
    QString m_requestedModelPointer;
    QQueue<QString> m_queue;
};

} // namespace talkinput
