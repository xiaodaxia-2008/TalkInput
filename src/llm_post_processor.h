#pragma once

#include "json_utils.h"
#include "spawn_llama_server.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <functional>
#include <memory>

class QNetworkReply;

namespace talkinput
{

class LlmPostProcessor final : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const QString &)>;

    explicit LlmPostProcessor(QObject *parent = nullptr);
    ~LlmPostProcessor() override;

    void postProcess(const QString &text, QObject *receiver, Callback callback);
    void postProcess(const QString &text, const QString &contextText,
                     const QString &hotwords, QObject *receiver,
                     Callback callback);

private:
    struct PendingRequest
    {
        QString text;
        QString contextText;
        QString hotwords;
        QPointer<QObject> receiver;
        Callback callback;
    };

    void shutdown();

    void ensureReady();
    void sendCompletion(const PendingRequest &request);
    void drainQueue();
    void failPending(const QString &reason);
    static QString cleanupResponseText(const QString &text);

    QNetworkAccessManager m_network;
    LlamaServerManager m_serverManager;
    QQueue<PendingRequest> m_pending;
};

} // namespace talkinput
