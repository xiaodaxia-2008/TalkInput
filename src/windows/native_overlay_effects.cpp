#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../native_overlay_effects.h"

#include <QWidget>

namespace talkinput
{

namespace
{

void enableAcrylic(HWND hwnd)
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (!hUser) {
        return;
    }

    using SetWindowCompositionAttribute = BOOL(WINAPI *)(HWND, void *);
    auto *setWindowCompositionAttribute =
        reinterpret_cast<SetWindowCompositionAttribute>(
            GetProcAddress(hUser, "SetWindowCompositionAttribute"));
    if (!setWindowCompositionAttribute) {
        return;
    }

    struct AccentPolicy
    {
        DWORD state;
        DWORD flags;
        DWORD color;
        DWORD animId;
    };

    struct WinCompAttrData
    {
        DWORD attr;
        const void *data;
        DWORD dataSize;
    };

    AccentPolicy accent = {4, 0, 0xC0101012, 0};
    WinCompAttrData wcad = {19, &accent, sizeof(accent)};
    setWindowCompositionAttribute(hwnd, &wcad);
}

} // namespace

void applyNativeOverlayEffects(QWidget *widget)
{
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    enableAcrylic(hwnd);
}

} // namespace talkinput
