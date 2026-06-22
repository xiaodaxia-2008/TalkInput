#pragma once

#include "json_utils.h"

#include <QCoro/QCoroTask>
#include <QFuture>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPromise>
#include <deque>
#include <memory>

namespace talkinput
{

class LlmPostProcessor final : public QObject
{
    Q_OBJECT

public:
    explicit LlmPostProcessor(QObject *parent = nullptr);
    ~LlmPostProcessor() override;

    QCoro::Task<QString> postProcess(const QString &text,
                                     const QString &contextText = {},
                                     const QString &hotwords = {});

private:
    struct PendingRequest
    {
        QString text;
        QString contextText;
        QString hotwords;
        std::unique_ptr<QPromise<QString>> promise;
    };

    void drainQueue();
    QCoro::Task<void> sendCompletion(PendingRequest request);
    void failPending(const QString &reason);
    static QString cleanupResponseText(const QString &text);

    QNetworkAccessManager m_network;
    std::deque<PendingRequest> m_pending;
};

} // namespace talkinput
