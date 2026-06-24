#include "voice_hotkey.h"
#include "app_config.h"
#include "logging.h"

#include <QHotkey>
#include <QKeySequence>

namespace talkinput
{

VoiceHotkey::VoiceHotkey(QObject *parent) : QObject(parent)
{
    registerShortcuts();
}

VoiceHotkey::~VoiceHotkey()
{
    unregisterShortcuts();
}

void VoiceHotkey::registerShortcuts()
{
    if (!QHotkey::isPlatformSupported()) {
        SPDLOG_WARN("Global hotkeys are not supported on this platform");
        return;
    }

    registerTriggerShortcut();
    registerModeSwitchShortcut();
}

void VoiceHotkey::registerTriggerShortcut()
{
    const QKeySequence keys(
        QString::fromStdString(appConfig().settings.triggerHotkey));

    if (keys.isEmpty()) {
        SPDLOG_DEBUG("Skipping empty trigger hotkey");
        return;
    }

    m_triggerHotkey = std::make_unique<QHotkey>(keys, true, this);

    connect(m_triggerHotkey.get(), &QHotkey::activated, this,
            [this]() { emit triggerActivated(); });

    if (!m_triggerHotkey->isRegistered()) {
        SPDLOG_WARN("Failed to register trigger hotkey {}", keys.toString());
    }
    else {
        SPDLOG_INFO("Trigger hotkey registered: {}", keys.toString());
    }
}

void VoiceHotkey::registerModeSwitchShortcut()
{
    const QKeySequence keys(
        QString::fromStdString(appConfig().settings.modeSwitchHotkey));

    if (keys.isEmpty()) {
        SPDLOG_DEBUG("Skipping empty mode-switch hotkey");
        return;
    }

    m_modeSwitchHotkey = std::make_unique<QHotkey>(keys, true, this);

    connect(m_modeSwitchHotkey.get(), &QHotkey::activated, this,
            [this]() { emit modeSwitchActivated(); });

    if (!m_modeSwitchHotkey->isRegistered()) {
        SPDLOG_WARN("Failed to register mode-switch hotkey {}",
                    keys.toString());
    }
    else {
        SPDLOG_INFO("Mode-switch hotkey registered: {}", keys.toString());
    }
}

void VoiceHotkey::reregisterTriggerShortcut()
{
    if (m_triggerHotkey) {
        m_triggerHotkey->setRegistered(false);
        m_triggerHotkey.reset();
    }
    registerTriggerShortcut();
}

void VoiceHotkey::reregisterModeSwitchShortcut()
{
    if (m_modeSwitchHotkey) {
        m_modeSwitchHotkey->setRegistered(false);
        m_modeSwitchHotkey.reset();
    }
    registerModeSwitchShortcut();
}

void VoiceHotkey::unregisterShortcuts()
{
    if (m_triggerHotkey) {
        m_triggerHotkey->setRegistered(false);
        m_triggerHotkey.reset();
    }
    if (m_modeSwitchHotkey) {
        m_modeSwitchHotkey->setRegistered(false);
        m_modeSwitchHotkey.reset();
    }
}

} // namespace talkinput
