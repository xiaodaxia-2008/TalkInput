#pragma once

#include "voice_input_controller.h"

#include <QKeySequence>
#include <QObject>
#include <array>
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
    void activated(PipelineMode mode);

private:
    void registerShortcuts();
    void unregisterShortcuts();
    void registerShortcut(PipelineMode mode);

    std::array<std::unique_ptr<QHotkey>, 3> m_hotkeys;
};

} // namespace talkinput
