#pragma once

#include <QString>
#include <QRect>
#include <QWindow>

namespace talkinput
{

/// Pastes text into the currently active input field.
/// Falls back from clipboard-based paste to key event injection when needed.
void pasteTextToActiveWindow(const QString &text, bool useClipboard,
                             bool copyToClipboard, bool restoreClipboard);

/// Returns the native window ID under the cursor, or 0 if not available.
/// Platform-specific implementations use native APIs (e.g. WindowFromPoint on
/// Windows) to find windows that Qt doesn't know about.
WId nativeWindowAtCursor();

/// Returns the bounding rect of the native window in virtual desktop
/// coordinates. Platform-specific (e.g. GetWindowRect on Windows).
QRect nativeWindowRect(WId wid);

} // namespace talkinput
