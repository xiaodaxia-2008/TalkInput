#include "spawn_llama_server.h"
#include "archive_utils.h"
#include "llm_config.h"
#include "logging.h"
#include "utils.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <spdlog/stopwatch.h>

namespace
{

struct LocalServiceInfo
{
    int port = 8765;
    int maxHealthAttempts = 120;
    QUrl archiveUrl;
};

LocalServiceInfo localServiceInfo()
{
    const auto provider = talkinput::currentLlmProviderPreset();
    return {
        .port = jsonInt(provider, "localServicePort", 8765),
        .maxHealthAttempts =
            jsonInt(provider, "localServiceMaxHealthAttempts", 120),
        .archiveUrl = QUrl(jsonString(
            provider, "localServiceArchiveUrl",
            QStringLiteral("https://github.com/ggml-org/llama.cpp/releases/"
                           "download/b9685/llama-b9685-bin-win-cpu-x64.zip"))),
    };
}

QNetworkRequest makeRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

// Resolve the local GGUF model info (fileName + url) for the configured
// managed-local LLM provider. Returns empty fields if not configured.
struct LocalModelInfo
{
    QString fileName;
    QString url;
};

LocalModelInfo localModelInfo()
{
    const nlohmann::json provider = talkinput::currentLlmProviderPreset();
    if (!provider.is_object() || provider.empty()) {
        return {};
    }

    const QString model = talkinput::llmProviderModel(provider);
    if (model.isEmpty()) {
        return {};
    }

    const nlohmann::json modelsInfo =
        provider.value("modelsInfo", nlohmann::json::object());
    if (!modelsInfo.is_object() || !modelsInfo.contains(model.toStdString())) {
        return {};
    }
    const nlohmann::json info = modelsInfo[model.toStdString()];
    if (!info.is_object()) {
        return {};
    }

    return {jsonString(info, "fileName"), jsonString(info, "url")};
}

} // namespace

void stopProcessListeningOnPort(quint16 port);

namespace talkinput
{

LlamaServerManager::LlamaServerManager(QObject *parent) : QObject(parent)
{
    connect(&m_network, &QNetworkAccessManager::finished, this,
            &LlamaServerManager::onDownloadFinished);
    connect(&m_healthTimer, &QTimer::timeout, this,
            &LlamaServerManager::pollHealth);
    m_healthTimer.setInterval(500);

    connect(&m_server, &QProcess::readyReadStandardError, this, [this]() {
        const QString text =
            QString::fromLocal8Bit(m_server.readAllStandardError());
        if (!text.trimmed().isEmpty()) {
            SPDLOG_DEBUG("llama-server stderr: {}", text.trimmed());
        }
    });
    connect(&m_server, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString text =
            QString::fromLocal8Bit(m_server.readAllStandardOutput());
        if (!text.trimmed().isEmpty()) {
            SPDLOG_DEBUG("llama-server stdout: {}", text.trimmed());
        }
    });
    connect(&m_server, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
                if (m_stopping) {
                    return;
                }
                emit failed(tr("Failed to start llama-server: %1")
                                .arg(m_server.errorString()));
            });
    connect(&m_server,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                &QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (m_stopping) {
                    return;
                }
                SPDLOG_WARN("llama-server exited: code {} status {}", exitCode,
                            static_cast<int>(exitStatus));
                m_ready = false;
                m_healthTimer.stop();
                emit failed(tr("LLM service stopped unexpectedly."));
            });
}

LlamaServerManager::~LlamaServerManager()
{
    stop();
}

void LlamaServerManager::start()
{
    m_stopping = false;

    if (m_ready && m_server.state() != QProcess::NotRunning) {
        emit ready();
        return;
    }

    if (m_preparing) {
        return;
    }

    prepare();
}

void LlamaServerManager::stop()
{
    m_stopping = true;
    m_healthTimer.stop();
    m_ready = false;
    m_preparing = false;
    if (m_activeDownload) {
        m_activeDownload->abort();
        m_activeDownload->deleteLater();
        m_activeDownload = nullptr;
    }
    if (m_server.state() != QProcess::NotRunning) {
        m_server.terminate();
        if (!m_server.waitForFinished(2000)) {
            m_server.kill();
        }
    }
}

bool LlamaServerManager::isReady() const
{
    return m_ready && m_server.state() != QProcess::NotRunning;
}

QString LlamaServerManager::baseDir() const
{
    return QDir(talkinput::appDataDir()).filePath("llm");
}

QString LlamaServerManager::llamaDir() const
{
    return QDir(baseDir()).filePath("llama.cpp");
}

QString LlamaServerManager::modelDir() const
{
    return QDir(baseDir()).filePath("models");
}

QString LlamaServerManager::llamaArchivePath() const
{
    return QDir(baseDir()).filePath("llama-b9685-bin-win-cpu-x64.zip");
}

QString LlamaServerManager::modelPath() const
{
    const LocalModelInfo info = localModelInfo();
    if (info.fileName.isEmpty()) {
        return {};
    }
    return QDir(modelDir()).filePath(info.fileName);
}

QString LlamaServerManager::serverExecutablePath() const
{
    QDirIterator it(llamaDir(), {"llama-server.exe"}, QDir::Files,
                    QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return it.next();
    }

    return {};
}

void LlamaServerManager::prepare()
{
    if (m_preparing) {
        return;
    }

    const auto service = localServiceInfo();

    stopProcessListeningOnPort(service.port);

    QDir().mkpath(llamaDir());
    QDir().mkpath(modelDir());

    if (serverExecutablePath().isEmpty()) {
        spdlog::get("statusbar")->info("{}", tr("Downloading LLM runtime..."));
        beginDownload(DownloadKind::LlamaArchive, service.archiveUrl,
                      llamaArchivePath());
        return;
    }

    const QString localModelPath = modelPath();
    if (localModelPath.isEmpty()) {
        emit failed(tr("LLM local model file name is not configured."));
        return;
    }

    if (!QFileInfo(localModelPath).isFile()) {
        spdlog::get("statusbar")->info("{}", tr("Downloading LLM model..."));
        const LocalModelInfo info = localModelInfo();
        if (info.url.isEmpty()) {
            emit failed(tr("LLM local model URL is not configured."));
            return;
        }
        beginDownload(DownloadKind::Model, QUrl(info.url), modelPath());
        return;
    }

    startServer();
}

void LlamaServerManager::beginDownload(DownloadKind kind, const QUrl &url,
                                       const QString &path)
{
    m_preparing = true;
    m_downloadKind = kind;
    m_activeDownloadPath = path;
    QFile::remove(path + ".part");
    m_downloadFile = std::make_unique<QFile>(path + ".part");
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        emit failed(tr("Cannot create LLM download file."));
        return;
    }

    QNetworkRequest request = makeRequest(url);
    m_activeDownload = m_network.get(request);
    connect(m_activeDownload, &QNetworkReply::readyRead, this, [this]() {
        if (m_activeDownload && m_downloadFile) {
            m_downloadFile->write(m_activeDownload->readAll());
        }
    });
    connect(m_activeDownload, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total <= 0) {
                    return;
                }
                const int percent = static_cast<int>(received * 100 / total);
                spdlog::get("statusbar")
                    ->info("{}",
                           tr("Downloading LLM component %1%...").arg(percent));
            });
    SPDLOG_INFO("LLM download started: {}", url.toString());
}

void LlamaServerManager::onDownloadFinished(QNetworkReply *reply)
{
    if (!reply || reply != m_activeDownload) {
        return;
    }

    if (m_downloadFile) {
        m_downloadFile->write(reply->readAll());
        m_downloadFile->close();
    }

    const bool failed = reply->error() != QNetworkReply::NoError;
    const QString errorText = reply->errorString();
    reply->deleteLater();
    m_activeDownload = nullptr;

    const QString tempPath = m_activeDownloadPath + ".part";
    if (failed) {
        QFile::remove(tempPath);
        m_downloadFile.reset();
        emit this->failed(tr("LLM download failed: %1").arg(errorText));
        return;
    }

    QFile::remove(m_activeDownloadPath);
    QFile::rename(tempPath, m_activeDownloadPath);
    m_downloadFile.reset();

    if (m_downloadKind == DownloadKind::LlamaArchive) {
        spdlog::get("statusbar")->info("{}", tr("Extracting LLM runtime..."));
        auto extractResult = extractLlamaArchive();
        if (!extractResult) {
            emit this->failed(tr("LLM runtime extraction failed: %1")
                                  .arg(extractResult.error()));
            return;
        }
    }

    m_downloadKind = DownloadKind::None;
    m_preparing = false;
    prepare();
}

std::expected<void, QString> LlamaServerManager::extractLlamaArchive()
{
    QDir(llamaDir()).removeRecursively();
    QDir().mkpath(llamaDir());
    return extractArchive(llamaArchivePath(), llamaDir());
}

void LlamaServerManager::startServer()
{
    if (m_server.state() != QProcess::NotRunning) {
        m_preparing = true;
        m_healthAttempts = 0;
        m_healthTimer.start();
        return;
    }

    const QString executable = serverExecutablePath();
    if (executable.isEmpty()) {
        emit failed(tr("llama-server.exe was not found."));
        return;
    }

    m_preparing = true;
    m_ready = false;
    const auto service = localServiceInfo();
    m_server.setProgram(executable);
    m_server.setWorkingDirectory(QFileInfo(executable).absolutePath());
    m_server.setArguments({"-m", modelPath(), "--host", "127.0.0.1", "--port",
                           QString::number(service.port), "-c", "1024"});

    spdlog::get("statusbar")->info("{}", tr("Starting LLM service..."));
    SPDLOG_INFO("Starting llama-server: {}", executable);
    m_server.start();
    m_healthAttempts = 0;
    m_healthTimer.start();
}

void LlamaServerManager::pollHealth()
{
    const auto service = localServiceInfo();

    ++m_healthAttempts;
    if (m_healthAttempts > service.maxHealthAttempts) {
        m_healthTimer.stop();
        emit failed(tr("LLM service did not become ready."));
        return;
    }

    QNetworkRequest request = makeRequest(
        QUrl(QString("http://127.0.0.1:%1/health").arg(service.port)));
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!ok) {
            return;
        }

        m_healthTimer.stop();
        m_preparing = false;
        m_ready = true;
        spdlog::get("statusbar")->info("{}", tr("LLM service ready"));
        emit ready();
    });
}

} // namespace talkinput
