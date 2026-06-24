#include "llm_post_processor.h"
#include "app_config.h"
#include "clean_text.h"
#include "logging.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCoro/QCoroFuture>
#include <QCoro/QCoroNetworkReply>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace talkinput
{

namespace
{
QNetworkRequest makeRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

void logLlmCost(const nlohmann::json &doc, const LlmPreset &provider,
                const QString &model)
{
    const auto it = provider.models.find(model.toStdString());
    if (it == provider.models.end()) {
        return;
    }
    const auto &price = it->second.price;
    if (price.inputPer1M <= 0) {
        return;
    }
    const double promptCacheHit = doc.value(
        nlohmann::json::json_pointer("/usage/prompt_cache_hit_tokens"), 0.0);
    const double promptCacheMiss = doc.value(
        nlohmann::json::json_pointer("/usage/prompt_cache_miss_tokens"), 0.0);
    const double outputTokens = doc.value(
        nlohmann::json::json_pointer("/usage/completion_tokens"), 0.0);
    const double totalTokens =
        doc.value(nlohmann::json::json_pointer("/usage/total_tokens"), 0.0);

    const double inputCost = promptCacheHit * price.cacheHitInputPer1M / 1e6 +
                             promptCacheMiss * price.cacheMissInputPer1M / 1e6;
    const double outputCost = outputTokens * price.outputPer1M / 1e6;
    const double totalCost = inputCost + outputCost;

    SPDLOG_INFO("LLM cost: {} tokens, ¥"
                "{:.6f} (cache hit: "
                "{:.0f} * ¥{:.4f}/M, cache "
                "miss: {:.0f} * ¥{:.4f}/M, "
                "output: {:.0f} * "
                "¥{:.4f}/M)",
                totalTokens, totalCost, promptCacheHit,
                price.cacheHitInputPer1M, promptCacheMiss,
                price.cacheMissInputPer1M, outputTokens, price.outputPer1M);
}

} // namespace

LlmPostProcessor::LlmPostProcessor(QObject *parent) : QObject(parent)
{
}

LlmPostProcessor::~LlmPostProcessor() = default;

QCoro::Task<QString> LlmPostProcessor::postProcess(const QString &text,
                                                   const QString &contextText,
                                                   const QString &hotwords)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        co_return text;
    }

    auto promise = std::make_unique<QPromise<QString>>();
    promise->start();
    QFuture<QString> future = promise->future();

    m_pending.emplace_back(PendingRequest{trimmed, contextText.trimmed(),
                                          hotwords.trimmed(),
                                          std::move(promise)});
    drainQueue();

    co_return co_await future;
}

void LlmPostProcessor::drainQueue()
{
    while (!m_pending.empty()) {
        sendCompletion(std::move(m_pending.front()));
        m_pending.pop_front();
    }
}

QCoro::Task<void> LlmPostProcessor::sendCompletion(PendingRequest request)
{
    QStringList lines = request.text.split('\n', Qt::SkipEmptyParts);
    const QString formattedInput = lines.join(", ");
    const QString cleanedContext = extractOcrWords(request.contextText);

    QString systemPrompt =
        QString::fromStdString(appConfig().settings.llmSystemPrompt);
    systemPrompt.replace("{{input}}", formattedInput);
    systemPrompt.replace("{{context}}", cleanedContext);
    systemPrompt.replace("{{hotwords}}", request.hotwords);

    QString userPrompt =
        QString::fromStdString(appConfig().settings.llmUserPrompt);
    userPrompt.replace("{{input}}", formattedInput);
    userPrompt.replace("{{context}}", cleanedContext);
    userPrompt.replace("{{hotwords}}", request.hotwords);

    const auto &provider =
        appConfig().llmPresets.at(appConfig().settings.llmProviderId);
    const QString model = QString::fromStdString(provider.currentModel);
    nlohmann::json payload = {{"messages",
                               {{{"role", "system"}, {"content", systemPrompt}},
                                {{"role", "user"}, {"content", userPrompt}}}},
                              {"model", model},
                              {"reasoning_effort", "low"},
                              {"extra_body", {"thinking", {"type", "enabled"}}},
                              {"temperature", 0.1},
                              {"max_tokens", 2000},
                              {"stream", false}};
    if (provider.managedLocalService) {
        payload["chat_template_kwargs"] = {{"enable_thinking", false}};
    }

    QNetworkRequest networkRequest =
        makeRequest(QUrl(QString::fromStdString(provider.endpoint)));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             "application/json");
    const QString apiKey = QString::fromStdString(provider.apiKey);
    if (!apiKey.isEmpty()) {
        networkRequest.setRawHeader("Authorization",
                                    QString("Bearer %1").arg(apiKey).toUtf8());
    }
    SPDLOG_DEBUG("LLM request:\n{}", payload.dump(2));

    const std::string requestJson = payload.dump();
    const QByteArray requestBody = QByteArray::fromStdString(requestJson);

    QNetworkReply *reply = m_network.post(networkRequest, requestBody);
    co_await reply;

    QString result = request.text;
    bool requestFailed = false;
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray responseBody = reply->readAll();
        try {
            const nlohmann::json doc = nlohmann::json::parse(
                responseBody.constData(),
                responseBody.constData() + responseBody.size());
            SPDLOG_DEBUG("LLM response: {}", doc.dump(2));

            const QString content = doc.value(
                nlohmann::json::json_pointer("/choices/0/message/content"),
                QString{});
            result = cleanupResponseText(content);
            logLlmCost(doc, provider, model);
        }
        catch (const nlohmann::json::exception &e) {
            SPDLOG_WARN("LLM response parse failed: {}", e.what());
            requestFailed = true;
        }
    }
    else {
        SPDLOG_WARN("LLM post-process failed: {}", reply->errorString());
        requestFailed = true;
    }
    reply->deleteLater();

    if (request.promise && !request.promise->isCanceled()) {
        request.promise->addResult(result);
        request.promise->finish();
    }
    STATUSBAR_INFO("{}", requestFailed ? tr("LLM post-processing failed; "
                                            "using original text.")
                                       : tr("LLM post-processing complete."));
}

void LlmPostProcessor::failPending(const QString &reason)
{
    SPDLOG_WARN("LLM post-processor fallback: {}", reason);
    STATUSBAR_INFO("{}", reason);
    while (!m_pending.empty()) {
        auto &request = m_pending.front();
        if (request.promise && !request.promise->isCanceled()) {
            request.promise->addResult(request.text);
            request.promise->finish();
        }
        m_pending.pop_front();
    }
}

QString LlmPostProcessor::cleanupResponseText(const QString &text)
{
    QString result = text.trimmed();
    if (result.isEmpty()) {
        return {};
    }
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
