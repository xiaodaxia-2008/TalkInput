#pragma once

#include <QString>

namespace talkinput
{

bool tryClipboardPaste(const QString &text);
void sendViaSendInput(const QString &text);
bool isTerminalWindow(void *nativeWindow);
void injectText(const QString &text);

} // namespace talkinput
