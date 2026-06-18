#include "llm_post_processor.h"
#include "app_config.h"
#include "logging.h"
#include "model_registry.h"

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
