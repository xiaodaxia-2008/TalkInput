#pragma once

#include <QObject>
#include <memory>

class QHotkey;

namespace zenny
{

class VoiceHotkey final : public QObject
{
    Q_OBJECT

public:
    explicit VoiceHotkey(QObject *parent = nullptr);
    ~VoiceHotkey() override;

    void reregisterTriggerShortcut();
    void reregisterModeSwitchShortcut();

signals:
    void triggerActivated();
    void modeSwitchActivated();

private:
    void registerShortcuts();
    void unregisterShortcuts();
    void registerTriggerShortcut();
    void registerModeSwitchShortcut();

    std::unique_ptr<QHotkey> m_triggerHotkey;
    std::unique_ptr<QHotkey> m_modeSwitchHotkey;
};

} // namespace zenny
