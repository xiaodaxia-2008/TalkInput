#pragma once

#include "ocr_recognizer.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <deque>

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
    void recognizeText(const QImage &image, QObject *receiver,
                       Callback callback) override;

private:
    struct PendingRequest
    {
        QTemporaryFile *tempFile = nullptr;
        QString path;
        QPointer<QObject> receiver;
        Callback callback;
    };

    QString serverScriptPath() const;
    QString scriptsDir() const;
    void ensureServiceStarted();
    void startService();
    void pollHealth();
    void flushPendingRequests();
    void failPendingRequests();
    void dispatchRequest(PendingRequest request);
    void completeRequest(QPointer<QObject> receiver, Callback callback,
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
