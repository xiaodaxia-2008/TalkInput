#pragma once

#include <QObject>
#include <memory>

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

    std::unique_ptr<QHotkey> m_hotkey;
};

} // namespace talkinput
