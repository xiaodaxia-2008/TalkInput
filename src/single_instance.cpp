#include "single_instance.h"

#include "logging.h"

#include <QDir>
#include <QLocalSocket>
#include <QStandardPaths>

namespace talkinput
{

SingleInstance::SingleInstance(QString applicationId, QObject *parent)
    : QObject(parent),
      m_serverName(QStringLiteral("ZenShawn.%1").arg(applicationId)),
      m_lockFile(
          QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
              .filePath(m_serverName + QStringLiteral(".lock")))
{
    m_lockFile.setStaleLockTime(5000);
    connect(&m_server, &QLocalServer::newConnection, this,
            &SingleInstance::handleConnection);
}

bool SingleInstance::start()
{
    if (!m_lockFile.tryLock()) {
        notifyPrimaryInstance();
        return false;
    }

    QLocalServer::removeServer(m_serverName);
    if (!m_server.listen(m_serverName)) {
        SPDLOG_ERROR("Failed to create single-instance server: {}",
                     m_server.errorString());
        m_lockFile.unlock();
        return false;
    }

    return true;
}

void SingleInstance::notifyPrimaryInstance() const
{
    QLocalSocket socket;
    socket.connectToServer(m_serverName, QIODevice::WriteOnly);
    if (!socket.waitForConnected(1000)) {
        SPDLOG_WARN("Another TalkInput instance is running, but it could not "
                    "be activated: {}",
                    socket.errorString());
        return;
    }

    socket.write("activate");
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
}

void SingleInstance::handleConnection()
{
    while (m_server.hasPendingConnections()) {
        QLocalSocket *socket = m_server.nextPendingConnection();
        if (!socket) {
            continue;
        }

        emit activationRequested();
        socket->disconnectFromServer();
        socket->deleteLater();
    }
}

} // namespace talkinput
