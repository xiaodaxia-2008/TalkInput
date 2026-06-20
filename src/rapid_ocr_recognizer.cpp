#include "rapid_ocr_recognizer.h"
#include "logging.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>
#include <QUrl>

namespace talkinput
{

namespace
{

constexpr int RapidOcrServicePort = 18765;
constexpr int RapidOcrHealthIntervalMs = 500;
constexpr int RapidOcrMaxHealthAttempts = 120;

QUrl serviceUrl(const QString &path)
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2")
                    .arg(RapidOcrServicePort)
                    .arg(path));
}

QNetworkRequest jsonRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    return request;
}

} // namespace

RapidOcrRecognizer::RapidOcrRecognizer(QObject *parent) : OcrRecognizer(parent)
{
    m_healthTimer.setInterval(RapidOcrHealthIntervalMs);
    connect(&m_healthTimer, &QTimer::timeout, this,
            &RapidOcrRecognizer::pollHealth);
}

RapidOcrRecognizer::~RapidOcrRecognizer()
{
    if (m_service.state() == QProcess::NotRunning) {
        return;
    }

    m_service.terminate();
    if (!m_service.waitForFinished(3000)) {
        m_service.kill();
        m_service.waitForFinished(3000);
    }
}

bool RapidOcrRecognizer::isAvailable() const
{
    return QFileInfo::exists(serverScriptPath());
}

void RapidOcrRecognizer::recognizeText(const QImage &image, QObject *receiver,
                                       Callback callback)
{
    if (!receiver || !callback || image.isNull()) {
        return;
    }

    auto *tempFile = new QTemporaryFile(this);
    tempFile->setAutoRemove(true);
    if (!tempFile->open()) {
        SPDLOG_WARN("RapidOcr: failed to create temp file");
        QMetaObject::invokeMethod(
            receiver, [callback = std::move(callback)]() { callback({}); },
            Qt::QueuedConnection);
        return;
    }

    const QString tempPath = QFileInfo(*tempFile).absoluteFilePath();
    if (!image.save(tempPath, "PNG")) {
        SPDLOG_WARN("RapidOcr: failed to save image to temp file");
        QMetaObject::invokeMethod(
            receiver, [callback = std::move(callback)]() { callback({}); },
            Qt::QueuedConnection);
        return;
    }
    tempFile->close();

    m_pendingRequests.push_back({.tempFile = tempFile,
                                 .path = tempPath,
                                 .receiver = QPointer<QObject>(receiver),
                                 .callback = std::move(callback)});

    ensureServiceStarted();
    if (m_ready) {
        flushPendingRequests();
    }
}

QString RapidOcrRecognizer::serverScriptPath() const
{
    return scriptsDir() + QStringLiteral("/rapidocr_server.py");
}

QString RapidOcrRecognizer::scriptsDir() const
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/scripts");
}

void RapidOcrRecognizer::ensureServiceStarted()
{
    if (m_ready || m_starting) {
        return;
    }

    if (m_service.state() != QProcess::NotRunning) {
        m_starting = true;
        m_healthAttempts = 0;
        m_healthTimer.start();
        return;
    }

    startService();
}

void RapidOcrRecognizer::startService()
{
    const QString scriptPath = serverScriptPath();
    if (!QFileInfo::exists(scriptPath)) {
        SPDLOG_WARN("RapidOcr: service script not found: {}", scriptPath);
        failPendingRequests();
        return;
    }

    connect(
        &m_service,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus) {
            const QString stderrText =
                QString::fromUtf8(m_service.readAllStandardError()).trimmed();
            SPDLOG_WARN("RapidOcr: service exited with code {}: {}", exitCode,
                        stderrText);
            m_ready = false;
            m_starting = false;
            m_healthTimer.stop();
            failPendingRequests();
        },
        Qt::UniqueConnection);

    m_starting = true;
    m_ready = false;
    m_healthAttempts = 0;
    m_service.setWorkingDirectory(scriptsDir());
    m_service.setProgram(QStringLiteral("uv"));
    m_service.setArguments({QStringLiteral("run"), scriptPath,
                            QStringLiteral("--port"),
                            QString::number(RapidOcrServicePort)});

    SPDLOG_INFO("RapidOcr: starting service: uv run {} --port {}", scriptPath,
                RapidOcrServicePort);
    m_service.start();
    m_healthTimer.start();
}

void RapidOcrRecognizer::pollHealth()
{
    ++m_healthAttempts;
    if (m_healthAttempts > RapidOcrMaxHealthAttempts) {
        SPDLOG_WARN("RapidOcr: service did not become ready");
        m_healthTimer.stop();
        m_starting = false;
        failPendingRequests();
        return;
    }

    auto *reply =
        m_network.get(jsonRequest(serviceUrl(QStringLiteral("/health"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const bool ok = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (!ok) {
            return;
        }

        m_healthTimer.stop();
        m_starting = false;
        m_ready = true;
        SPDLOG_INFO("RapidOcr: service ready");
        flushPendingRequests();
    });
}

void RapidOcrRecognizer::flushPendingRequests()
{
    while (!m_pendingRequests.empty()) {
        auto request = std::move(m_pendingRequests.front());
        m_pendingRequests.pop_front();
        dispatchRequest(std::move(request));
    }
}

void RapidOcrRecognizer::failPendingRequests()
{
    while (!m_pendingRequests.empty()) {
        auto request = std::move(m_pendingRequests.front());
        m_pendingRequests.pop_front();
        delete request.tempFile;
        completeRequest(request.receiver, std::move(request.callback), {});
    }
}

void RapidOcrRecognizer::dispatchRequest(PendingRequest request)
{
    QJsonObject body;
    body.insert(QStringLiteral("path"), request.path);

    auto *reply =
        m_network.post(jsonRequest(serviceUrl(QStringLiteral("/ocr"))),
                       QJsonDocument(body).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, request = std::move(request)]() mutable {
                QString text;
                if (reply->error() == QNetworkReply::NoError) {
                    const auto document =
                        QJsonDocument::fromJson(reply->readAll());
                    text = document.object()
                               .value(QStringLiteral("text"))
                               .toString()
                               .trimmed();
                }
                else {
                    SPDLOG_WARN("RapidOcr: request failed: {}",
                                reply->errorString());
                    m_ready = false;
                }

                reply->deleteLater();
                delete request.tempFile;
                completeRequest(request.receiver, std::move(request.callback),
                                text);
            });
}

void RapidOcrRecognizer::completeRequest(QPointer<QObject> receiver,
                                         Callback callback, const QString &text)
{
    if (!receiver) {
        return;
    }

    QMetaObject::invokeMethod(
        receiver.data(),
        [callback = std::move(callback), text]() mutable { callback(text); },
        Qt::QueuedConnection);
}

} // namespace talkinput
