#pragma once

#include "ocr_recognizer.h"

#include <QNetworkAccessManager>
#include <QProcess>
#include <QTimer>
#include <deque>
#include <memory>

template <typename T>
class QPromise;

class QNetworkReply;
class QTemporaryFile;

namespace talkinput
{

class RapidOcrRecognizer final : public OcrRecognizer
{
    Q_OBJECT

public:
    explicit RapidOcrRecognizer(QObject *parent = nullptr);
    ~RapidOcrRecognizer() override;

    bool isAvailable() const override;
    QCoro::Task<QString> recognizeText(const QImage &image) override;

private:
    struct PendingRequest
    {
        QTemporaryFile *tempFile = nullptr;
        QString path;
        std::shared_ptr<QPromise<QString>> promise;
    };

    QString serverScriptPath() const;
    QString scriptsDir() const;
    void ensureServiceStarted();
    void startService();
    void pollHealth();
    void flushPendingRequests();
    void failPendingRequests();
    void dispatchRequest(PendingRequest request);
    void completeRequest(std::shared_ptr<QPromise<QString>> promise,
                         const QString &text);

    QNetworkAccessManager m_network;
    QProcess m_service;
    QTimer m_healthTimer;
    std::deque<PendingRequest> m_pendingRequests;
    bool m_ready = false;
    bool m_starting = false;
    int m_healthAttempts = 0;
};

} // namespace talkinput
