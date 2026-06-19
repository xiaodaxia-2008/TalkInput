#include "llm_post_processor.h"
#include "app_config.h"
#include "logging.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

namespace
{

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

const nlohmann::json llmProvidersJson()
{
    return talkinput::appConfigValue("llmPostProcessing/providers");
}

nlohmann::json firstLlmProviderJson()
{
    const nlohmann::json providers = llmProvidersJson();
    if (providers.is_array() && !providers.empty()) {
        return providers.front();
    }
    return nlohmann::json::object();
}

nlohmann::json findLlmProviderJson(const QString &id)
{
    const nlohmann::json providers = llmProvidersJson();
    if (providers.is_array()) {
        for (const auto &provider : providers) {
            if (provider.is_object() &&
                provider.value("id", std::string()) == id.toStdString())
            {
                return provider;
            }
        }
    }
    return nlohmann::json::object();
}

} // namespace

namespace talkinput
{

LlmPostProcessor::LlmPostProcessor(QObject *parent) : QObject(parent)
{
    if (QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                this, &LlmPostProcessor::shutdown);
    }

    connect(&m_serverManager, &LlamaServerManager::statusMessage, this,
            &LlmPostProcessor::statusMessage);
    connect(&m_serverManager, &LlamaServerManager::ready, this,
            &LlmPostProcessor::drainQueue);
    connect(&m_serverManager, &LlamaServerManager::failed, this,
            &LlmPostProcessor::failPending);
}

LlmPostProcessor::~LlmPostProcessor()
{
    shutdown();
}

void LlmPostProcessor::shutdown()
{
    m_serverManager.stop();
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

nlohmann::json LlmPostProcessor::configuredProvider() const
{
    const QString providerId =
        appConfigString("settings/llm/providerId").trimmed();
    nlohmann::json provider = findLlmProviderJson(providerId);
    if (provider.is_object() && !provider.empty()) {
        return provider;
    }
    return firstLlmProviderJson();
}

QString LlmPostProcessor::configuredEndpoint() const
{
    const QString endpoint =
        appConfigString("settings/llm/endpoint").trimmed();
    if (!endpoint.isEmpty()) {
        return endpoint;
    }

    const nlohmann::json provider = configuredProvider();
    if (provider.value("custom", false)) {
        return appConfigString("settings/llm/customEndpoint").trimmed();
    }

    return qs(provider.value("endpoint", std::string())).trimmed();
}

QString LlmPostProcessor::configuredModel() const
{
    const QString configuredModel =
        appConfigString("settings/llm/model").trimmed();
    if (!configuredModel.isEmpty()) {
        return configuredModel;
    }

    const nlohmann::json provider = configuredProvider();
    const QString providerId = qs(provider.value("id", std::string()));
    if (provider.value("custom", false)) {
        return appConfigString("settings/llm/customModel").trimmed();
    }

    const QString providerModel =
        appConfigString(llmProviderModelKey(providerId)).trimmed();
    if (!providerModel.isEmpty()) {
        return providerModel;
    }
    return qs(provider.value("model", std::string())).trimmed();
}

QString LlmPostProcessor::configuredApiKey() const
{
    return appConfigString("settings/llm/apiKey").trimmed();
}

bool LlmPostProcessor::usesManagedLocalService() const
{
    return configuredProvider().value("managedLocalService", false);
}

void LlmPostProcessor::ensureReady()
{
    if (!usesManagedLocalService()) {
        drainQueue();
        return;
    }

    if (m_serverManager.isReady()) {
        drainQueue();
        return;
    }

    m_serverManager.start();
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
    QString systemPrompt = appConfigString("settings/llm/systemPrompt");
    systemPrompt.replace("{{input}}", formattedInput);
    systemPrompt.replace("{{context}}", cleanedContext);
    systemPrompt.replace("{{hotwords}}", request.hotwords);

    // ---- User prompt (template replacement) ----
    QString userPrompt = appConfigString("settings/llm/userPrompt");
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
    const nlohmann::json provider = configuredProvider();
    const nlohmann::json providerPricing =
        provider.value("pricing", nlohmann::json::object());
    const nlohmann::json pricing =
        providerPricing.contains(modelName) &&
                providerPricing[modelName].is_object()
            ? providerPricing[modelName]
            : nlohmann::json::object();
    const double inputPer1M = pricing.value("inputPer1M", 0.0);
    const double cacheHitInputPer1M = pricing.value("cacheHitInputPer1M", 0.0);
    const double cacheMissInputPer1M = pricing.value("cacheMissInputPer1M", 0.0);
    const double outputPer1M = pricing.value("outputPer1M", 0.0);

    QNetworkReply *reply = m_network.post(networkRequest, requestBody);
    const PendingRequest pendingCopy = request;
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, pendingCopy, inputPer1M, cacheHitInputPer1M,
         cacheMissInputPer1M, outputPer1M]() mutable {
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
                    if (!usage.empty() && inputPer1M > 0) {
                        const double promptCacheHit =
                            usage.value("prompt_cache_hit_tokens", 0.0);
                        const double promptCacheMiss =
                            usage.value("prompt_cache_miss_tokens", 0.0);
                        const double outputTokens =
                            usage.value("completion_tokens", 0.0);
                        const double totalTokens =
                            usage.value("total_tokens", 0.0);

                        const double inputCost =
                            promptCacheHit * cacheHitInputPer1M / 1e6 +
                            promptCacheMiss * cacheMissInputPer1M / 1e6;
                        const double outputCost =
                            outputTokens * outputPer1M / 1e6;
                        const double totalCost = inputCost + outputCost;

                        SPDLOG_INFO("LLM cost: {} tokens, $"
                                    "{:.6f} (cache hit: "
                                    "{:.0f} * ${:.4f}/M, cache "
                                    "miss: {:.0f} * ${:.4f}/M, "
                                    "output: {:.0f} * "
                                    "${:.4f}/M)",
                                    totalTokens, totalCost, promptCacheHit,
                                    cacheHitInputPer1M, promptCacheMiss,
                                    cacheMissInputPer1M, outputTokens,
                                    outputPer1M);
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
