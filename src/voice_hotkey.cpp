#include "voice_hotkey.h"
#include "logging.h"

#include <QHotkey>
#include <QKeySequence>

namespace talkinput
{

VoiceHotkey::VoiceHotkey(QObject *parent) : QObject(parent)
{
    registerShortcut();
}

VoiceHotkey::~VoiceHotkey()
{
    unregisterShortcut();
}

void VoiceHotkey::registerShortcut()
{
    if (!QHotkey::isPlatformSupported()) {
        SPDLOG_WARN("Global hotkeys are not supported on this platform");
        return;
    }

    m_hotkey =
        new QHotkey(QKeySequence(QStringLiteral("Ctrl+Alt+L")), true, this);
    connect(m_hotkey, &QHotkey::activated, this, &VoiceHotkey::activated);

    if (!m_hotkey->isRegistered()) {
        SPDLOG_WARN("Failed to register global hotkey: Ctrl+Alt+L");
    }
    else {
        SPDLOG_INFO("Global hotkey registered: Ctrl+Alt+L");
    }
}

void VoiceHotkey::unregisterShortcut()
{
    if (!m_hotkey) {
        return;
    }

    m_hotkey->setRegistered(false);
    delete m_hotkey;
    m_hotkey = nullptr;
}

} // namespace talkinput
