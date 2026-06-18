#include "llm_post_processor.h"
#include "archive_utils.h"
#include "logging.h"
#include "model_registry.h"

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
// clang-format on
#endif

namespace
{

constexpr int ServerPort = 8765;
constexpr int MaxHealthAttempts = 120;
const QUrl LlamaArchiveUrl(
    "https://github.com/ggml-org/llama.cpp/releases/download/b9685/"
    "llama-b9685-bin-win-cpu-x64.zip");

QNetworkRequest makeRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

#ifdef Q_OS_WIN
void stopProcessListeningOnPort(quint16 port)
{
    using GetExtendedTcpTableFn =
        DWORD(WINAPI *)(PVOID, PDWORD, BOOL, ULONG, TCP_TABLE_CLASS, ULONG);

    HMODULE module = LoadLibraryW(L"iphlpapi.dll");
    if (!module) {
        spdlog::warn("Cannot load iphlpapi.dll to inspect LLM server port");
        return;
    }

    auto *getExtendedTcpTable = reinterpret_cast<GetExtendedTcpTableFn>(
        GetProcAddress(module, "GetExtendedTcpTable"));
    if (!getExtendedTcpTable) {
        spdlog::warn("Cannot resolve GetExtendedTcpTable");
        FreeLibrary(module);
        return;
    }

    DWORD size = 0;
    DWORD ret = getExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
                                    TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (ret != ERROR_INSUFFICIENT_BUFFER) {
        FreeLibrary(module);
        return;
    }

    QByteArray buffer;
    buffer.resize(static_cast<qsizetype>(size));
    auto *table = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
    ret = getExtendedTcpTable(table, &size, FALSE, AF_INET,
                              TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (ret != NO_ERROR) {
        spdlog::warn("GetExtendedTcpTable failed: {}", ret);
        FreeLibrary(module);
        return;
    }

    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto &row = table->table[i];
        const auto localPort =
            static_cast<quint16>(ntohs(static_cast<u_short>(row.dwLocalPort)));
        if (localPort != port) {
            continue;
        }

        const DWORD pid = row.dwOwningPid;
        if (pid == 0 || pid == GetCurrentProcessId()) {
            continue;
        }

        HANDLE process =
            OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!process) {
            spdlog::warn("Cannot open process {} listening on LLM port {}", pid,
                         port);
            continue;
        }

        spdlog::warn("Terminating existing process {} listening on LLM port {}",
                     pid, port);
        if (TerminateProcess(process, 0)) {
            WaitForSingleObject(process, 3000);
        }
        else {
            spdlog::warn("TerminateProcess failed for pid {}", pid);
        }
        CloseHandle(process);
    }

    FreeLibrary(module);
}
#else
void stopProcessListeningOnPort(quint16)
{
}
#endif

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
                if (m_stopping) {
                    return;
                }
                failPending(tr("Failed to start llama-server: %1")
                                .arg(m_server.errorString()));
            });
    connect(&m_server,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(
                &QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
                if (m_stopping) {
                    return;
                }
                spdlog::warn("llama-server exited: code {} status {}", exitCode,
                             static_cast<int>(exitStatus));
                m_serverReady = false;
                m_healthTimer.stop();
                if (!m_pending.isEmpty()) {
                    failPending(tr("LLM service stopped unexpectedly."));
                }
            });
}

LlmPostProcessor::~LlmPostProcessor()
{
    m_stopping = true;
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

    const QString inputText = text.trimmed();
    spdlog::debug("LLM post-process queued input: {}", inputText);
    m_pending.enqueue({inputText, receiver, std::move(callback)});
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
    const LlmLocalModel model = loadLlmLocalModel();
    if (model.fileName.isEmpty()) {
        return {};
    }
    return QDir(modelDir()).filePath(model.fileName);
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

QString LlmPostProcessor::configuredEndpoint() const
{
    QSettings settings;
    const QString endpoint =
        settings.value("llm/endpoint", defaultLlmEndpoint())
            .toString()
            .trimmed();
    return endpoint.isEmpty() ? defaultLlmEndpoint() : endpoint;
}

QString LlmPostProcessor::configuredModel() const
{
    QSettings settings;
    const QString model =
        settings.value("llm/model", defaultLlmModel()).toString().trimmed();
    return model.isEmpty() ? defaultLlmModel() : model;
}

QString LlmPostProcessor::configuredApiKey() const
{
    QSettings settings;
    return settings.value("llm/apiKey").toString().trimmed();
}

bool LlmPostProcessor::usesManagedLocalService() const
{
    const QUrl endpoint(configuredEndpoint());
    return endpoint.scheme() == "http" && endpoint.host() == "127.0.0.1" &&
           endpoint.port(ServerPort) == ServerPort &&
           endpoint.path() == "/v1/chat/completions";
}

void LlmPostProcessor::ensureReady()
{
    if (!usesManagedLocalService()) {
        drainQueue();
        return;
    }

    if (m_serverReady && m_server.state() != QProcess::NotRunning) {
        drainQueue();
        return;
    }
    m_serverReady = false;
    if (m_preparing) {
        return;
    }

    prepareManagedLocalService();
}

void LlmPostProcessor::prepareManagedLocalService()
{
    if (m_preparing) {
        return;
    }

    stopProcessListeningOnPort(ServerPort);

    QDir().mkpath(llamaDir());
    QDir().mkpath(modelDir());

    if (serverExecutablePath().isEmpty()) {
        emit statusMessage(tr("Downloading LLM runtime..."));
        beginDownload(DownloadKind::LlamaArchive, LlamaArchiveUrl,
                      llamaArchivePath());
        return;
    }

    const QString localModelPath = modelPath();
    if (localModelPath.isEmpty()) {
        failPending(tr("LLM local model file name is not configured."));
        return;
    }

    if (!QFileInfo(localModelPath).isFile()) {
        emit statusMessage(tr("Downloading LLM model..."));
        const LlmLocalModel model = loadLlmLocalModel();
        if (model.url.isEmpty()) {
            failPending(tr("LLM local model URL is not configured."));
            return;
        }
        beginDownload(DownloadKind::Model, QUrl(model.url), modelPath());
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

    spdlog::debug("LLM post-process input: {}", request.text);

    const QString systemPrompt =
        "你是语音识别文本后处理器。修正错别字、同音误识别、"
        "标点和自然断句。不要解释，不要添加原文没有的信息，只"
        "返回修正后的文本。";
    const QString userPrompt =
        QString("请后处理这段语音识别文本：\n%1").arg(request.text);

    spdlog::debug("LLM system prompt: {}", systemPrompt);
    spdlog::debug("LLM user prompt: {}", userPrompt);

    QJsonArray messages;
    messages.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
    messages.append(QJsonObject{{"role", "user"}, {"content", userPrompt}});

    QJsonObject payload{{"messages", messages},
                        {"model", configuredModel()},
                        {"temperature", 0.1},
                        {"max_tokens", 512},
                        {"stream", false}};
    if (usesManagedLocalService()) {
        payload.insert("chat_template_kwargs",
                       QJsonObject{{"enable_thinking", false}});
    }

    QNetworkRequest networkRequest = makeRequest(QUrl(configuredEndpoint()));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             "application/json");
    const QString apiKey = configuredApiKey();
    if (!apiKey.isEmpty()) {
        networkRequest.setRawHeader("Authorization",
                                    QString("Bearer %1").arg(apiKey).toUtf8());
    }
    const QByteArray requestBody =
        QJsonDocument(payload).toJson(QJsonDocument::Compact);
    spdlog::debug("LLM chat request JSON: {}", QString::fromUtf8(requestBody));

    auto *reply = m_network.post(networkRequest, requestBody);
    connect(reply, &QNetworkReply::finished, this, [reply, request]() mutable {
        QString result = request.text;
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray responseBody = reply->readAll();
            spdlog::debug("LLM chat response JSON: {}",
                          QString::fromUtf8(responseBody));
            const QJsonDocument doc = QJsonDocument::fromJson(responseBody);
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
        spdlog::debug("LLM post-process output: {}", result);
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
    const QString thinkEnd = "</think>";
    const qsizetype thinkEndIndex = result.indexOf(thinkEnd);
    if (thinkEndIndex >= 0) {
        result = result.mid(thinkEndIndex + thinkEnd.size()).trimmed();
    }
    if (result.startsWith('"') && result.endsWith('"') && result.size() >= 2) {
        result = result.mid(1, result.size() - 2).trimmed();
    }
    return result;
}

} // namespace talkinput
