#include "llm_post_processor.h"
#include "logging.h"

#include <archive.h>
#include <archive_entry.h>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>

namespace
{

constexpr int ServerPort = 8765;
constexpr int MaxHealthAttempts = 120;
const QUrl LlamaArchiveUrl(
    "https://github.com/ggml-org/llama.cpp/releases/download/b9685/"
    "llama-b9685-bin-win-cpu-x64.zip");
const QUrl ModelUrl(
    "https://huggingface.co/bartowski/Qwen2.5-0.5B-Instruct-GGUF/resolve/"
    "main/Qwen2.5-0.5B-Instruct-Q4_K_M.gguf");
const char *ModelFileName = "Qwen2.5-0.5B-Instruct-Q4_K_M.gguf";

QNetworkRequest makeRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

bool isPathInsideDir(const QString &path, const QString &dir)
{
    const QString absPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString absDir = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    return absPath == absDir || absPath.startsWith(absDir + '/') ||
           absPath.startsWith(absDir + '\\');
}

QString entryPath(struct archive_entry *entry)
{
    const char *utf8 = archive_entry_pathname_utf8(entry);
    return utf8 ? QString::fromUtf8(utf8)
                : QString::fromLocal8Bit(archive_entry_pathname(entry));
}

bool extractArchive(const QString &archivePath, const QString &destDir,
                    QString *errorMessage)
{
    QDir dest(destDir);
    if (!dest.exists() && !dest.mkpath(".")) {
        if (errorMessage) {
            *errorMessage = QString("Cannot create: %1").arg(destDir);
        }
        return false;
    }

    archive *reader = archive_read_new();
    archive_read_support_filter_all(reader);
    archive_read_support_format_all(reader);

    if (archive_read_open_filename(
            reader,
            QDir::toNativeSeparators(archivePath).toLocal8Bit().constData(),
            10240) != ARCHIVE_OK)
    {
        if (errorMessage) {
            *errorMessage =
                QString::fromLocal8Bit(archive_error_string(reader));
        }
        archive_read_free(reader);
        return false;
    }

    archive_entry *entry = nullptr;
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        const QString rel = QDir::cleanPath(entryPath(entry));
        if (rel.isEmpty() || rel.startsWith('/') || rel.startsWith("..")) {
            if (errorMessage) {
                *errorMessage = QString("Unsafe path: %1").arg(rel);
            }
            archive_read_free(reader);
            return false;
        }

        const QString outPath = dest.filePath(rel);
        if (!isPathInsideDir(outPath, dest.absolutePath())) {
            if (errorMessage) {
                *errorMessage = QString("Escapes destination: %1").arg(rel);
            }
            archive_read_free(reader);
            return false;
        }

        const auto fileType = archive_entry_filetype(entry);
        if (fileType == AE_IFDIR) {
            QDir().mkpath(outPath);
            archive_read_data_skip(reader);
            continue;
        }
        if (fileType != AE_IFREG) {
            archive_read_data_skip(reader);
            continue;
        }

        QDir().mkpath(QFileInfo(outPath).absolutePath());
        QFile outFile(outPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (errorMessage) {
                *errorMessage = QString("Cannot write: %1").arg(outPath);
            }
            archive_read_free(reader);
            return false;
        }

        const void *buffer = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        while (archive_read_data_block(reader, &buffer, &size, &offset) ==
               ARCHIVE_OK)
        {
            Q_UNUSED(offset);
            if (outFile.write(static_cast<const char *>(buffer),
                              static_cast<qint64>(size)) !=
                static_cast<qint64>(size))
            {
                if (errorMessage) {
                    *errorMessage = QString("Write error: %1").arg(outPath);
                }
                archive_read_free(reader);
                return false;
            }
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    archive_read_free(reader);
    return true;
}

} // namespace

namespace talkinput
{

LlmPostProcessor::LlmPostProcessor(QObject *parent) : QObject(parent)
{
    connect(&m_network, &QNetworkAccessManager::finished, this,
            &LlmPostProcessor::onDownloadFinished);
    connect(&m_healthTimer, &QTimer::timeout, this,
            &LlmPostProcessor::pollHealth);
    m_healthTimer.setInterval(500);
}

LlmPostProcessor::~LlmPostProcessor()
{
    if (m_activeDownload) {
        m_activeDownload->abort();
        m_activeDownload->deleteLater();
    }
    if (m_server.state() != QProcess::NotRunning) {
        m_server.terminate();
        if (!m_server.waitForFinished(2000)) {
            m_server.kill();
        }
    }
}

bool LlmPostProcessor::isEnabled() const
{
    QSettings settings;
    return settings.value("llm/postProcessingEnabled", false).toBool();
}

void LlmPostProcessor::postProcess(const QString &text, QObject *receiver,
                                   Callback callback)
{
    if (text.trimmed().isEmpty() || !isEnabled()) {
        callback(text);
        return;
    }

    m_pending.enqueue({text.trimmed(), receiver, std::move(callback)});
    ensureReady();
}

QString LlmPostProcessor::baseDir() const
{
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QDir::current().filePath("cache");
    }
    return QDir(base).filePath("llm");
}

QString LlmPostProcessor::llamaDir() const
{
    return QDir(baseDir()).filePath("llama.cpp");
}

QString LlmPostProcessor::modelDir() const
{
    return QDir(baseDir()).filePath("models");
}

QString LlmPostProcessor::llamaArchivePath() const
{
    return QDir(baseDir()).filePath("llama-b9685-bin-win-cpu-x64.zip");
}

QString LlmPostProcessor::modelPath() const
{
    return QDir(modelDir()).filePath(ModelFileName);
}

QString LlmPostProcessor::serverExecutablePath() const
{
    QDirIterator it(llamaDir(), {"llama-server.exe"}, QDir::Files,
                    QDirIterator::Subdirectories);
    if (it.hasNext()) {
        return it.next();
    }

    return {};
}

void LlmPostProcessor::ensureReady()
{
    if (m_serverReady) {
        drainQueue();
        return;
    }
    if (m_preparing) {
        return;
    }

    QDir().mkpath(llamaDir());
    QDir().mkpath(modelDir());

    if (serverExecutablePath().isEmpty()) {
        emit statusMessage(tr("Downloading LLM runtime..."));
        beginDownload(DownloadKind::LlamaArchive, LlamaArchiveUrl,
                      llamaArchivePath());
        return;
    }

    if (!QFileInfo(modelPath()).isFile()) {
        emit statusMessage(tr("Downloading LLM model..."));
        beginDownload(DownloadKind::Model, ModelUrl, modelPath());
        return;
    }

    startServer();
}

void LlmPostProcessor::beginDownload(DownloadKind kind, const QUrl &url,
                                     const QString &path)
{
    m_preparing = true;
    m_downloadKind = kind;
    m_activeDownloadPath = path;
    QFile::remove(path + ".part");
    m_downloadFile = std::make_unique<QFile>(path + ".part");
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        failPending(tr("Cannot create LLM download file."));
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
                emit statusMessage(
                    tr("Downloading LLM component %1%...").arg(percent));
            });
    spdlog::info("LLM download started: {}", url.toString());
}

void LlmPostProcessor::onDownloadFinished(QNetworkReply *reply)
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
        failPending(tr("LLM download failed: %1").arg(errorText));
        return;
    }

    QFile::remove(m_activeDownloadPath);
    QFile::rename(tempPath, m_activeDownloadPath);
    m_downloadFile.reset();

    if (m_downloadKind == DownloadKind::LlamaArchive) {
        emit statusMessage(tr("Extracting LLM runtime..."));
        QString error;
        if (!extractLlamaArchive(&error)) {
            failPending(tr("LLM runtime extraction failed: %1").arg(error));
            return;
        }
    }

    m_downloadKind = DownloadKind::None;
    m_preparing = false;
    ensureReady();
}

bool LlmPostProcessor::extractLlamaArchive(QString *errorMessage)
{
    QDir(llamaDir()).removeRecursively();
    QDir().mkpath(llamaDir());
    return extractArchive(llamaArchivePath(), llamaDir(), errorMessage);
}

void LlmPostProcessor::startServer()
{
    if (m_server.state() != QProcess::NotRunning) {
        m_preparing = true;
        m_healthAttempts = 0;
        m_healthTimer.start();
        return;
    }

    const QString executable = serverExecutablePath();
    if (executable.isEmpty()) {
        failPending(tr("llama-server.exe was not found."));
        return;
    }

    m_preparing = true;
    m_serverReady = false;
    m_server.setProgram(executable);
    m_server.setWorkingDirectory(QFileInfo(executable).absolutePath());
    m_server.setArguments({"-m", modelPath(), "--host", "127.0.0.1", "--port",
                           QString::number(ServerPort), "-c", "1024"});
    connect(&m_server, &QProcess::readyReadStandardError, this, [this]() {
        const QString text =
            QString::fromLocal8Bit(m_server.readAllStandardError());
        if (!text.trimmed().isEmpty()) {
            spdlog::debug("llama-server stderr: {}", text.trimmed());
        }
    });
    connect(&m_server, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString text =
            QString::fromLocal8Bit(m_server.readAllStandardOutput());
        if (!text.trimmed().isEmpty()) {
            spdlog::debug("llama-server stdout: {}", text.trimmed());
        }
    });
    connect(&m_server, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
                failPending(tr("Failed to start llama-server: %1")
                                .arg(m_server.errorString()));
            });

    emit statusMessage(tr("Starting LLM service..."));
    spdlog::info("Starting llama-server: {}", executable);
    m_server.start();
    m_healthAttempts = 0;
    m_healthTimer.start();
}

void LlmPostProcessor::pollHealth()
{
    ++m_healthAttempts;
    if (m_healthAttempts > MaxHealthAttempts) {
        m_healthTimer.stop();
        failPending(tr("LLM service did not become ready."));
        return;
    }

    QNetworkRequest request = makeRequest(
        QUrl(QString("http://127.0.0.1:%1/health").arg(ServerPort)));
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!ok) {
            return;
        }

        m_healthTimer.stop();
        m_preparing = false;
        m_serverReady = true;
        emit statusMessage(tr("LLM service ready."));
        drainQueue();
    });
}

void LlmPostProcessor::drainQueue()
{
    while (!m_pending.isEmpty()) {
        sendCompletion(m_pending.dequeue());
    }
}

void LlmPostProcessor::sendCompletion(const PendingRequest &request)
{
    if (!request.receiver) {
        return;
    }

    QJsonArray messages;
    messages.append(QJsonObject{
        {"role", "system"},
        {"content", "你是语音识别文本后处理器。修正错别字、同音误识别、"
                    "标点和自然断句。不要解释，不要添加原文没有的信息，只"
                    "返回修正后的文本。"}});
    messages.append(QJsonObject{
        {"role", "user"},
        {"content",
         QString("请后处理这段语音识别文本：\n%1").arg(request.text)}});

    const QJsonObject payload{{"messages", messages},
                              {"temperature", 0.1},
                              {"max_tokens", 512},
                              {"stream", false}};

    QNetworkRequest networkRequest = makeRequest(QUrl(
        QString("http://127.0.0.1:%1/v1/chat/completions").arg(ServerPort)));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             "application/json");
    auto *reply = m_network.post(
        networkRequest, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, request]() mutable {
        QString result = request.text;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            const QJsonArray choices = doc.object().value("choices").toArray();
            if (!choices.isEmpty()) {
                const QJsonObject message =
                    choices.first().toObject().value("message").toObject();
                const QString content =
                    message.value("content").toString().trimmed();
                if (!content.isEmpty()) {
                    result = cleanupResponseText(content);
                }
            }
        }
        else {
            spdlog::warn("LLM post-process failed: {}", reply->errorString());
        }
        reply->deleteLater();
        if (request.receiver && request.callback) {
            request.callback(result);
        }
    });
}

void LlmPostProcessor::failPending(const QString &reason)
{
    spdlog::warn("LLM post-processor fallback: {}", reason);
    emit statusMessage(reason);
    m_preparing = false;
    m_serverReady = false;
    m_healthTimer.stop();
    while (!m_pending.isEmpty()) {
        auto request = m_pending.dequeue();
        if (request.receiver && request.callback) {
            request.callback(request.text);
        }
    }
}

QString LlmPostProcessor::cleanupResponseText(const QString &text)
{
    QString result = text.trimmed();
    if (result.startsWith('"') && result.endsWith('"') && result.size() >= 2) {
        result = result.mid(1, result.size() - 2).trimmed();
    }
    return result;
}

} // namespace talkinput
