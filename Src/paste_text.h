#pragma once
#include <QString>

namespace talkinput
{

void pasteTextToActiveWindow(const QString &text, bool useClipboard,
                             bool restoreClipboard);
}
