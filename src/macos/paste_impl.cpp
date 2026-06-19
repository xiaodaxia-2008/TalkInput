#include "paste_impl.h"

namespace talkinput
{

bool tryClipboardPaste(const QString &)
{
    return false;
}

void sendViaSendInput(const QString &)
{
}

bool isTerminalWindow(void *)
{
    return false;
}

void injectText(const QString &)
{
}

} // namespace talkinput
