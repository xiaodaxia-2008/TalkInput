#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QString>

namespace zenny
{

class SingleInstance final : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QString applicationId, QObject *parent = nullptr);

    bool start();

signals:
    void activationRequested();

private:
    void notifyPrimaryInstance() const;
    void handleConnection();

    QString m_serverName;
    QLockFile m_lockFile;
    QLocalServer m_server;
};

} // namespace zenny
