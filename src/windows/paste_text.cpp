#include "../paste_text.h"

#include <QClipboard>
#include <QApplication>
#include <QMimeData>
#include <QThread>
#include <QVector>
#include <QString>

#include <Windows.h>

namespace talkinput
{

static bool tryClipboardPaste(const QString &text)
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        return false;
    }

    if (!OpenClipboard(nullptr)) {
        return false;
    }

    EmptyClipboard();

    const int len = text.length();
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>(len + 1) *
                                                      sizeof(wchar_t));
    if (!hGlobal) {
        CloseClipboard();
        return false;
    }

    auto *dst = static_cast<wchar_t *>(GlobalLock(hGlobal));
    text.toWCharArray(dst);
    dst[len] = L'\0';
    GlobalUnlock(hGlobal);

    if (!SetClipboardData(CF_UNICODETEXT, hGlobal)) {
        GlobalFree(hGlobal);
        CloseClipboard();
        return false;
    }
    CloseClipboard();

    const DWORD seqBefore = GetClipboardSequenceNumber();
    for (int tries = 0; tries < 50; ++tries) {
        if (GetClipboardSequenceNumber() != seqBefore) {
            break;
        }
        Sleep(10);
    }
    Sleep(20);

    INPUT ctrlV[4] = {};
    ctrlV[0].type = INPUT_KEYBOARD;
    ctrlV[0].ki.wVk = VK_CONTROL;
    ctrlV[1].type = INPUT_KEYBOARD;
    ctrlV[1].ki.wVk = 'V';
    ctrlV[2].type = INPUT_KEYBOARD;
    ctrlV[2].ki.wVk = 'V';
    ctrlV[2].ki.dwFlags = KEYEVENTF_KEYUP;
    ctrlV[3].type = INPUT_KEYBOARD;
    ctrlV[3].ki.wVk = VK_CONTROL;
    ctrlV[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, ctrlV, sizeof(INPUT));

    return true;
}

static void sendViaSendInput(const QString &text)
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        return;
    }

    DWORD tid = GetWindowThreadProcessId(hwnd, nullptr);
    DWORD ourTid = GetCurrentThreadId();
    const BOOL attached =
        (tid != ourTid) ? AttachThreadInput(ourTid, tid, TRUE) : FALSE;

    HIMC himc = ImmGetContext(hwnd);
    const BOOL wasOpen = himc ? ImmGetOpenStatus(himc) : FALSE;
    if (himc) {
        ImmSetOpenStatus(himc, FALSE);
    }

    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        if (code == 0) {
            continue;
        }

        INPUT pair[2] = {};
        pair[0].type = INPUT_KEYBOARD;
        pair[0].ki.wVk = 0;
        pair[0].ki.wScan = code;
        pair[0].ki.dwFlags = KEYEVENTF_UNICODE;

        pair[1].type = INPUT_KEYBOARD;
        pair[1].ki.wVk = 0;
        pair[1].ki.wScan = code;
        pair[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        SendInput(2, pair, sizeof(INPUT));

        if (code < 0x80) {
            Sleep(15);
        }
    }

    if (himc) {
        ImmSetOpenStatus(himc, wasOpen);
        ImmReleaseContext(hwnd, himc);
    }

    if (attached) {
        AttachThreadInput(ourTid, tid, FALSE);
    }
}

static bool isTerminalWindow(HWND hwnd)
{
    wchar_t cls[256];
    if (!GetClassNameW(hwnd, cls, 256)) {
        return false;
    }
    return wcscmp(cls, L"ConsoleWindowClass") == 0 ||
           wcscmp(cls, L"CASCADIA_HOSTING_WINDOW_CLASS") == 0;
}

void pasteTextToActiveWindow(const QString &text, bool useClipboard,
                             bool copyToClipboard, bool restoreClipboard)
{
    if (text.isEmpty()) {
        return;
    }

    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        return;
    }

    if (useClipboard) {
        if (isTerminalWindow(hwnd)) {
            if (!tryClipboardPaste(text)) {
                sendViaSendInput(text);
            }
        }
        else {
            // Use Qt clipboard + Ctrl+V with optional restore
            QClipboard *clipboard = QApplication::clipboard();
            QMimeData *originalData = nullptr;

            // Save original clipboard only when we intend to restore it
            const bool shouldRestore = restoreClipboard && !copyToClipboard;
            if (shouldRestore) {
                const QMimeData *currentData = clipboard->mimeData();
                originalData = new QMimeData();
                for (const QString &format : currentData->formats()) {
                    originalData->setData(format, currentData->data(format));
                }
            }

            clipboard->setText(text);

            INPUT inputs[4] = {};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wVk = VK_CONTROL;
            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wVk = 'V';
            inputs[2].type = INPUT_KEYBOARD;
            inputs[2].ki.wVk = 'V';
            inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
            inputs[3].type = INPUT_KEYBOARD;
            inputs[3].ki.wVk = VK_CONTROL;
            inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(4, inputs, sizeof(INPUT));

            if (shouldRestore && originalData) {
                QThread::msleep(50);
                clipboard->setMimeData(originalData);
            }
        }
    }
    else {
        sendViaSendInput(text);

        if (copyToClipboard) {
            QClipboard *clipboard = QApplication::clipboard();
            clipboard->setText(text);
        }
    }
}

} // namespace talkinput
