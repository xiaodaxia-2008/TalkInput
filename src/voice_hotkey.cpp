#include "voice_hotkey.h"
#include "logging.h"

#include <QHotkey>

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

    registerShortcut(PipelineMode::AsrOnly);
    registerShortcut(PipelineMode::AsrLlm);
    registerShortcut(PipelineMode::AsrLlmOcr);
}

void VoiceHotkey::registerShortcut(PipelineMode mode)
{
    const int idx = static_cast<int>(mode);
    const QKeySequence keys = hotkeySequence(mode);

    if (keys.isEmpty()) {
        SPDLOG_DEBUG("Skipping empty hotkey for mode {}", idx);
        return;
    }

    m_hotkeys[idx] =
        std::make_unique<QHotkey>(keys, true, this);

    connect(m_hotkeys[idx].get(), &QHotkey::activated, this,
            [this, mode]() { emit activated(mode); });

    if (!m_hotkeys[idx]->isRegistered()) {
        SPDLOG_WARN("Failed to register hotkey {} for mode {}",
                    keys.toString(), idx);
    }
    else {
        SPDLOG_INFO("Hotkey registered: {} (mode {})", keys.toString(), idx);
    }
}

void VoiceHotkey::unregisterShortcuts()
{
    for (auto &hk : m_hotkeys) {
        if (hk) {
            hk->setRegistered(false);
            hk.reset();
        }
    }
}

} // namespace talkinput
