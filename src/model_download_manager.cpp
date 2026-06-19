#include "model_download_manager.h"
#include "app_config.h"
#include "archive_utils.h"
#include "asr_config.h"
#include "json_utils.h"
#include "utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace talkinput
{

ModelDownloadManager::ModelDownloadManager(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
}

bool ModelDownloadManager::startAsrModelDownload(const QString &modelPointer,
                                                 QString *errorMessage)
{
    if (m_reply) {
        if (errorMessage) {
            *errorMessage = tr("A model download is already running.");
        }
        return false;
    }

    m_queue.clear();
    if (!enqueueAsrModelDownloads(modelPointer, errorMessage)) {
        return false;
    }

    m_requestedModelPointer = modelPointer;
    startNextDownload();
    return true;
}

bool ModelDownloadManager::enqueueAsrModelDownloads(const QString &modelPointer,
                                                    QString *errorMessage)
{
    const nlohmann::json model = appConfigValue(modelPointer.toStdString());
    if (!model.is_object()) {
        if (errorMessage) {
            *errorMessage = tr("Model preset is invalid.");
        }
        return false;
    }

    const QUrl url(jsonString(model, "url"));
    if (url.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Model download URL is empty.");
        }
        return false;
    }

    QDir cache(QDir(appDataDir()).filePath(QStringLiteral("models")));
    if (!cache.exists() && !cache.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = tr("Failed to create model cache directory.");
        }
        return false;
    }

    m_queue.enqueue(modelPointer);

    const nlohmann::json punct =
        model.value("postPunctuationModel", nlohmann::json::object());
    if (punct.is_object() && !punct.empty() && !isAsrPresetInstalled(punct) &&
        !QUrl(jsonString(punct, "url")).isEmpty())
    {
        m_queue.enqueue(modelPointer + QStringLiteral("/postPunctuationModel"));
    }

    return true;
}

void ModelDownloadManager::startNextDownload()
{
    if (m_queue.isEmpty()) {
        emit finished(m_requestedModelPointer);
        m_requestedModelPointer.clear();
        return;
    }

    m_activeModelPointer = m_queue.dequeue();
    const nlohmann::json model =
        appConfigValue(m_activeModelPointer.toStdString());
    if (!model.is_object()) {
        emit downloadFailed({});
        clearActiveDownload();
        return;
    }

    const QUrl url(jsonString(model, "url"));
    const QString modelName = jsonString(model, "name");
    const QString archiveName = QFileInfo(url.path()).fileName();
    const QString modelRoot =
        QDir(appDataDir()).filePath(QStringLiteral("models"));

    m_archivePath = QDir(modelRoot).filePath(archiveName);
    m_tempPath = m_archivePath + QStringLiteral(".part");

    QFile::remove(m_tempPath);
    m_file = std::make_unique<QFile>(m_tempPath);
    if (!m_file->open(QIODevice::WriteOnly)) {
        emit downloadFailed(modelName);
        clearActiveDownload();
        return;
    }

    emit downloadStarted(modelName);
    m_reply = m_network->get(QNetworkRequest(url));
    connect(m_reply, &QNetworkReply::finished, this,
            &ModelDownloadManager::onDownloadFinished);
}

void ModelDownloadManager::onDownloadFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    const nlohmann::json model =
        appConfigValue(m_activeModelPointer.toStdString());
    const QString modelName = jsonString(model, "name");

    if (m_file && reply) {
        m_file->write(reply->readAll());
        m_file->close();
    }

    const bool failed = !reply || reply->error() != QNetworkReply::NoError;
    if (reply) {
        reply->deleteLater();
    }
    m_file.reset();

    if (failed) {
        emit downloadFailed(modelName);
        clearActiveDownload();
        return;
    }

    QDir dest(QDir(appDataDir()).filePath(QStringLiteral("models")));
    if (!dest.exists() && !dest.mkpath(QStringLiteral("."))) {
        emit extractionFailed(tr("Failed to create model directory."));
        clearActiveDownload();
        return;
    }

    emit extracting();
    auto result = extractArchive(m_archivePath, dest.absolutePath());
    QFile::remove(m_archivePath);

    if (!result) {
        emit extractionFailed(result.error());
        clearActiveDownload();
        return;
    }

    m_activeModelPointer.clear();
    startNextDownload();
}

void ModelDownloadManager::clearActiveDownload()
{
    m_file.reset();
    m_archivePath.clear();
    m_tempPath.clear();
    m_activeModelPointer.clear();
    m_requestedModelPointer.clear();
    m_queue.clear();
}

} // namespace talkinput
