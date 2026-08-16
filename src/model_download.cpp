#include "model_download.h"
#include "archive_utils.h"
#include "logging.h"
#include "utils.h"

#include <QCoreApplication>
#include <QCoro/QCoroNetworkReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace talkinput
{

QString asrModelLabel(const AsrPreset &m)
{
    auto langLabel = [](const QString &c) -> QString {
        if (c == QLatin1StringView("zh")) {
            return QStringLiteral("CN");
        }
        if (c == QLatin1StringView("en")) {
            return QStringLiteral("EN");
        }
        if (c == QLatin1StringView("zh,en")) {
            return QStringLiteral("CN/EN");
        }
        if (c == QLatin1StringView("multilingual")) {
            return QCoreApplication::translate("ModelDownload", "Multilingual");
        }
        if (c == QLatin1StringView("system")) {
            return {};
        }
        return c;
    };

    return QStringLiteral("%1 - %2 - %3")
        .arg(QString::fromStdString(m.name),
             m.streamingSupport
                 ? QCoreApplication::translate("ModelDownload", "Real-time")
                 : QCoreApplication::translate("ModelDownload", "Offline"),
             langLabel(QString::fromStdString(m.languages)));
}

bool isModelInstalled(const std::string &modelDirName,
                      const std::map<std::string, std::string> &files)
{
    if (modelDirName.empty()) {
        return false;
    }

    const auto checkDir = [&](const QString &baseDir) -> bool {
        const QString modelDir = QDir(baseDir).filePath(
            QStringLiteral("models/%1")
                .arg(QString::fromStdString(modelDirName)));
        if (!QFileInfo(modelDir).isDir()) {
            return false;
        }
        for (const auto &[key, relative] : files) {
            const QFileInfo fi(
                QDir(modelDir).filePath(QString::fromStdString(relative)));
            if (!fi.exists()) {
                return false;
            }
        }
        return true;
    };

    // Check binary directory first (bundled with installation),
    // then fall back to user data directory (downloaded at runtime).
    const QString exeDir = QCoreApplication::applicationDirPath();
    if (checkDir(exeDir)) {
        return true;
    }
    if (checkDir(appDataDir())) {
        return true;
    }

    SPDLOG_INFO("Model not found in binary dir nor data dir: {}", modelDirName);
    return false;
}

QCoro::Task<ModelArchiveResult> downloadModelArchive(const QString &modelLabel,
                                                     const QString &url,
                                                     const QString &serviceName)
{
    ModelArchiveResult result;

    if (url.isEmpty()) {
        result.error = QCoreApplication::translate("ModelDownload",
                                                   "Model preset is invalid.");
        co_return result;
    }

    QDir modelRoot(QDir(appDataDir()).filePath(QStringLiteral("models")));
    if (!modelRoot.exists() && !modelRoot.mkpath(QStringLiteral("."))) {
        result.error = QCoreApplication::translate(
            "ModelDownload", "Failed to create model cache directory.");
        co_return result;
    }

    const QUrl qurl(url);
    const QString archiveName = QFileInfo(qurl.path()).fileName();
    const QString archivePath = modelRoot.filePath(archiveName);

    QNetworkAccessManager manager;
    manager.setTransferTimeout(600000);
    QNetworkRequest request(qurl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);
    int lastPct = -1;
    QObject::connect(
        reply, &QNetworkReply::downloadProgress,
        [modelLabel, serviceName, &lastPct](qint64 received, qint64 total) {
            if (total <= 0) {
                return;
            }
            const int pct = static_cast<int>(received * 100 / total);
            if (pct == lastPct) {
                return;
            }
            lastPct = pct;
            STATUSBAR_INFO(
                "{}", QCoreApplication::translate(
                          "ModelDownload", "Downloading %1 model: %2 … %3%")
                          .arg(serviceName, modelLabel)
                          .arg(pct));
        });
    co_await reply;
    reply->setParent(nullptr);

    bool downloadOk = (reply->error() == QNetworkReply::NoError);
    QString downloadError = reply->errorString();

    if (downloadOk) {
        QFile file(archivePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
        }
        else {
            downloadOk = false;
            downloadError = file.errorString();
        }
    }
    reply->deleteLater();

    if (!downloadOk) {
        result.error = downloadError;
        co_return result;
    }

    STATUSBAR_INFO("{}", QCoreApplication::translate("ModelDownload",
                                                     "Extracting %1 model: %2")
                             .arg(serviceName, modelLabel));
    auto extractResult = extractArchive(archivePath, modelRoot.absolutePath());
    QFile::remove(archivePath);
    if (!extractResult) {
        result.error = extractResult.error();
        co_return result;
    }

    result.ok = true;
    co_return result;
}

} // namespace talkinput
