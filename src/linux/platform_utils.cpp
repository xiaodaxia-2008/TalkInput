#include "../platform_utils.h"

namespace talkinput
{

void pasteTextToActiveWindow(const QString &, bool, bool, bool)
{
}

WId nativeWindowAtCursor()
{
    return 0;
}

QRect nativeWindowRect(WId)
{
    return {};
}

} // namespace talkinput
