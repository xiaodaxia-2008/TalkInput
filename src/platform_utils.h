#pragma once

#include <QRect>
#include <QString>
#include <QWindow>

namespace talkinput
{
struct SysWindowInfo
{
    WId wid;        // Qt 的 Window ID (其实就是 HWND)
    QString title;  // 窗口标题
    QRect geometry; // 窗口矩形坐标
};

/// Pastes text into the currently active input field.
/// Falls back from clipboard-based paste to key event injection when needed.
void pasteTextToActiveWindow(const QString &text, bool useClipboard,
                             bool copyToClipboard, bool restoreClipboard);

SysWindowInfo nativeWindowInfoAtCursor();

} // namespace talkinput
