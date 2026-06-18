#include "llm_post_processor.h"
#include "app_config.h"
#include "archive_utils.h"
#include "logging.h"
#include "model_registry.h"
#include "utils.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
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

#include <spdlog/stopwatch.h>

namespace
{

constexpr int ServerPort = 8765;
constexpr int MaxHealthAttempts = 120;
const QUrl LlamaArchiveUrl(
    "https://github.com/ggml-org/llama.cpp/releases/download/b9685/"
    "llama-b9685-bin-win-cpu-x64.zip");

QString extractOcrWords(const QString &text)
{
    if (text.isEmpty()) {
        return {};
    }
    // Match continuous runs of Chinese characters or ASCII alphanumeric.
    // This strips punctuation, symbols, and whitespace, keeping only words.
    static const QRegularExpression re(
        QStringLiteral("[\\x{4e00}-\\x{9fff}]+|[a-zA-Z0-9]+"));
    QStringList words;
    auto it = QRegularExpressionMatchIterator(re.globalMatch(text));
    while (it.hasNext()) {
        words << it.next().captured();
    }
    return words.join(' ');
}

QString llmProviderModelKey(const QString &providerId)
{
    return QString("settings/llm/providerModels/%1").arg(providerId);
}

QString qs(const std::string &value)
{
    return QString::fromStdString(value);
}

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
        SPDLOG_WARN("Cannot load iphlpapi.dll to inspect LLM server port");
        return;
    }

    auto *getExtendedTcpTable = reinterpret_cast<GetExtendedTcpTableFn>(
        GetProcAddress(module, "GetExtendedTcpTable"));
    if (!getExtendedTcpTable) {
        SPDLOG_WARN("Cannot resolve GetExtendedTcpTable");
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
        SPDLOG_WARN("GetExtendedTcpTable failed: {}", ret);
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
            SPDLOG_WARN("Cannot open process {} listening on LLM port {}", pid,
                        port);
            continue;
        }

        SPDLOG_WARN("Terminating existing process {} listening on LLM port {}",
                    pid, port);
        if (TerminateProcess(process, 0)) {
            WaitForSingleObject(process, 3000);
        }
        else {
            SPDLOG_WARN("TerminateProcess failed for pid {}", pid);
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
    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                this, &LlmPostProcessor::shutdown);
    }

    connect(&m_network, &QNetworkAccessManager::finished, this,
            &LlmPostProcessor::onDownloadFinished);
    connect(&m_healthTimer, &QTimer::timeout, this,
            &LlmPostProcessor::pollHealth);
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
                SPDLOG_WARN("llama-server exited: code {} status {}", exitCode,
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
    shutdown();
}

void LlmPostProcessor::shutdown()
{
    m_stopping = true;
    m_healthTimer.stop();
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

bool LlmPostProcessor::isEnabled() const
{
    return appConfigBool("settings/llm/postProcessingEnabled", false);
}

void LlmPostProcessor::postProcess(const QString &text, QObject *receiver,
                                   Callback callback)
{
    postProcess(text, {}, {}, receiver, std::move(callback));
}

void LlmPostProcessor::postProcess(const QString &text,
                                   const QString &contextText,
                                   const QString &hotwords, QObject *receiver,
                                   Callback callback)
{
    if (text.trimmed().isEmpty() || !isEnabled()) {
        callback(text);
        return;
    }

    const QString inputText = text.trimmed();
    SPDLOG_DEBUG("LLM post-process queued input: {}", inputText);
    m_pending.enqueue({inputText, contextText.trimmed(), hotwords.trimmed(),
                       receiver, std::move(callback)});
    ensureReady();
}

QString LlmPostProcessor::baseDir() const
{
    return QDir(talkinput::appDataDir()).filePath("llm");
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
    if (model.fileName.empty()) {
        return {};
    }
    return QDir(modelDir()).filePath(qs(model.fileName));
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

LlmProviderPreset LlmPostProcessor::configuredProvider() const
{
    QString providerId =
        appConfigString("settings/llm/providerId", qs(defaultLlmProviderId()))
            .trimmed();
    const QString savedEndpoint =
        appConfigString("settings/llm/endpoint").trimmed();
    if (!appConfigContains("settings/llm/providerId") &&
        !savedEndpoint.isEmpty() && savedEndpoint != qs(defaultLlmEndpoint()))
    {
        providerId = "custom";
    }
    return findLlmProviderPreset(providerId.toStdString());
}

QString LlmPostProcessor::configuredEndpoint() const
{
    const LlmProviderPreset provider = configuredProvider();
    if (!provider.custom) {
        const QString endpoint = qs(provider.endpoint).trimmed();
        return endpoint.isEmpty() ? qs(defaultLlmEndpoint()) : endpoint;
    }

    return appConfigString("settings/llm/endpoint").trimmed();
}

QString LlmPostProcessor::configuredModel() const
{
    const LlmProviderPreset provider = configuredProvider();
    const QString providerModel =
        appConfigString(llmProviderModelKey(qs(provider.id))).trimmed();
    if (!providerModel.isEmpty()) {
        return providerModel;
    }
    const QString configuredModel =
        appConfigString("settings/llm/model").trimmed();
    if (!configuredModel.isEmpty()) {
        return configuredModel;
    }
    const QString model = qs(provider.model).trimmed();
    return model.isEmpty() ? qs(defaultLlmModel()) : model;
}

QString LlmPostProcessor::configuredApiKey() const
{
    return appConfigString("settings/llm/apiKey").trimmed();
}

bool LlmPostProcessor::usesManagedLocalService() const
{
    return configuredProvider().managedLocalService;
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
        if (model.url.empty()) {
            failPending(tr("LLM local model URL is not configured."));
            return;
        }
        beginDownload(DownloadKind::Model, QUrl(qs(model.url)), modelPath());
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
    SPDLOG_INFO("LLM download started: {}", url.toString());
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
    SPDLOG_INFO("Starting llama-server: {}", executable);
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
    // ---- Split input text by lines and join with commas ----
    QStringList lines = request.text.split('\n', Qt::SkipEmptyParts);
    const QString formattedInput = lines.join(", ");

    // ---- Clean OCR context: extract only words (Chinese + alphanumeric) ----
    const QString cleanedContext = extractOcrWords(request.contextText);

    // ---- System prompt (template replacement) ----
    QString systemPrompt =
        appConfigString("settings/llm/systemPrompt").trimmed();
    if (systemPrompt.isEmpty()) {
        systemPrompt = qs(defaultLlmSystemPrompt()).trimmed();
    }
    systemPrompt.replace("{{input}}", formattedInput);
    systemPrompt.replace("{{context}}", cleanedContext);
    systemPrompt.replace("{{hotwords}}", request.hotwords);

    // ---- User prompt (template replacement) ----
    QString userPrompt = appConfigString("settings/llm/userPrompt").trimmed();
    if (userPrompt.isEmpty()) {
        userPrompt = qs(defaultLlmUserPrompt()).trimmed();
    }
    userPrompt.replace("{{input}}", formattedInput);
    userPrompt.replace("{{context}}", cleanedContext);
    userPrompt.replace("{{hotwords}}", request.hotwords);

    nlohmann::json payload = {{"messages",
                               {{{"role", "system"}, {"content", systemPrompt}},
                                {{"role", "user"}, {"content", userPrompt}}}},
                              {"model", configuredModel()},
                              {"reasoning_effort", "low"},
                              {"extra_body", {"thinking", {"type", "enabled"}}},
                              {"temperature", 0.1},
                              {"max_tokens", 2000},
                              {"stream", false}};
    if (usesManagedLocalService()) {
        payload["chat_template_kwargs"] = {{"enable_thinking", false}};
    }

    QNetworkRequest networkRequest = makeRequest(QUrl(configuredEndpoint()));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             "application/json");
    const QString apiKey = configuredApiKey();
    if (!apiKey.isEmpty()) {
        networkRequest.setRawHeader("Authorization",
                                    QString("Bearer %1").arg(apiKey).toUtf8());
    }
    SPDLOG_DEBUG("LLM chat request body:\n{}", payload.dump(2));

    const std::string requestJson = payload.dump();
    const QByteArray requestBody = QByteArray::fromStdString(requestJson);

    const std::string modelName = configuredModel().toStdString();
    const auto provider = configuredProvider();
    auto pricingIt = provider.modelPricing.find(modelName);
    const LlmPricing pricing = pricingIt != provider.modelPricing.end()
                                   ? pricingIt->second
                                   : LlmPricing();

    QNetworkReply *reply = m_network.post(networkRequest, requestBody);
    const PendingRequest pendingCopy = request;
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, pendingCopy, pricing]() mutable {
            QString result = pendingCopy.text;
            bool requestFailed = false;
            if (reply->error() == QNetworkReply::NoError) {
                const QByteArray responseBody = reply->readAll();
                try {
                    const nlohmann::json doc = nlohmann::json::parse(
                        responseBody.constData(),
                        responseBody.constData() + responseBody.size());
                    SPDLOG_DEBUG("LLM chat response JSON: {}", doc.dump(2));

                    // ---- Extract content ----
                    const auto &choices =
                        doc.value("choices", nlohmann::json::array());
                    if (!choices.empty()) {
                        const auto &message = choices.front().value(
                            "message", nlohmann::json::object());
                        const QString content =
                            message.value("content", QString()).trimmed();
                        if (!content.isEmpty()) {
                            result = this->cleanupResponseText(content);
                        }
                    }

                    // ---- Token usage & cost calculation ----
                    const auto &usage =
                        doc.value("usage", nlohmann::json::object());
                    if (!usage.empty() && pricing.inputPer1M > 0) {
                        const double promptCacheHit =
                            usage.value("prompt_cache_hit_tokens", 0.0);
                        const double promptCacheMiss =
                            usage.value("prompt_cache_miss_tokens", 0.0);
                        const double outputTokens =
                            usage.value("completion_tokens", 0.0);
                        const double totalTokens =
                            usage.value("total_tokens", 0.0);

                        const double inputCost =
                            promptCacheHit * pricing.cacheHitInputPer1M / 1e6 +
                            promptCacheMiss * pricing.cacheMissInputPer1M / 1e6;
                        const double outputCost =
                            outputTokens * pricing.outputPer1M / 1e6;
                        const double totalCost = inputCost + outputCost;

                        SPDLOG_INFO("LLM cost: {} tokens, $"
                                    "{:.6f} (cache hit: "
                                    "{:.0f} * ${:.4f}/M, cache "
                                    "miss: {:.0f} * ${:.4f}/M, "
                                    "output: {:.0f} * "
                                    "${:.4f}/M)",
                                    totalTokens, totalCost, promptCacheHit,
                                    pricing.cacheHitInputPer1M, promptCacheMiss,
                                    pricing.cacheMissInputPer1M, outputTokens,
                                    pricing.outputPer1M);
                    }
                }
                catch (const nlohmann::json::exception &e) {
                    SPDLOG_WARN("LLM response parse failed: {}", e.what());
                    requestFailed = true;
                }
            }
            else {
                SPDLOG_WARN("LLM post-process failed: {}",
                            reply->errorString());
                requestFailed = true;
            }
            SPDLOG_DEBUG("LLM post-process output: {}", result);
            reply->deleteLater();
            if (pendingCopy.receiver && pendingCopy.callback) {
                pendingCopy.callback(result);
            }
            emit this->statusMessage(
                requestFailed ? tr("LLM post-processing failed; using "
                                   "original text.")
                              : tr("LLM post-processing complete."));
        });
}

void LlmPostProcessor::failPending(const QString &reason)
{
    SPDLOG_WARN("LLM post-processor fallback: {}", reason);
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
