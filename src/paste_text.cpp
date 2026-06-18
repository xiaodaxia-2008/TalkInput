#include "paste_text.h"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QThread>
#include <QVector>
#include <Windows.h>

namespace talkinput
{

void pasteTextToActiveWindow(const QString &text, bool useClipboard,
                             bool restoreClipboard)
{
    if (text.isEmpty()) {
        return;
    }

    if (useClipboard) {
        QClipboard *clipboard = QApplication::clipboard();
        QMimeData *originalData = nullptr;

        // 1. 如果需要还原，先深拷贝当前剪切板的完整数据
        if (restoreClipboard) {
            const QMimeData *currentData = clipboard->mimeData();
            originalData = new QMimeData();

            // 复制所有支持的格式（包含图片、文件路径、HTML 等）
            foreach (const QString &format, currentData->formats()) {
                originalData->setData(format, currentData->data(format));
            }
        }

        // 2. 将新文本设置到系统剪切板
        clipboard->setText(text);

        // 3. 模拟键盘发送 Ctrl + V
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

        // 4. 还原剪切板
        if (restoreClipboard && originalData) {
            // 给系统和目标窗口留出极短的时间来处理 Ctrl+V 请求
            QThread::msleep(50);

            // 恢复原始数据（QClipboard 会自动接管并释放 originalData 的内存）
            clipboard->setMimeData(originalData);
        }
    }
    else {
        // Unicode 直接发送模式（本身就不影响剪切板，无需处理还原逻辑）
        std::wstring wstr = text.toStdWString();
        int eventCount = wstr.length() * 2;
        QVector<INPUT> inputs(eventCount);

        for (int i = 0; i < wstr.length(); ++i) {
            int baseIdx = i * 2;

            inputs[baseIdx].type = INPUT_KEYBOARD;
            inputs[baseIdx].ki.wVk = 0;
            inputs[baseIdx].ki.wScan = wstr[i];
            inputs[baseIdx].ki.dwFlags = KEYEVENTF_UNICODE;

            inputs[baseIdx + 1].type = INPUT_KEYBOARD;
            inputs[baseIdx + 1].ki.wVk = 0;
            inputs[baseIdx + 1].ki.wScan = wstr[i];
            inputs[baseIdx + 1].ki.dwFlags =
                KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        }

        SendInput(inputs.size(), inputs.data(), sizeof(INPUT));
    }
}
} // namespace talkinput
