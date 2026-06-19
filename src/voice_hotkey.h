#pragma once

#include <QObject>

class QHotkey;

namespace talkinput
{

class VoiceHotkey final : public QObject
{
    Q_OBJECT

public:
    explicit VoiceHotkey(QObject *parent = nullptr);
    ~VoiceHotkey() override;

signals:
    void activated();

private:
    void registerShortcut();
    void unregisterShortcut();

    QHotkey *m_hotkey = nullptr;
};

} // namespace talkinput
