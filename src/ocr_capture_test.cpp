#include "logging.h"

#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace
{

struct CaptureTarget
{
    HWND hwnd = nullptr;
    RECT windowRect = {};
    QString windowTitle;
    QString className;
};

int parseDelaySeconds(const QStringList &args)
{
    for (int i = 1; i + 1 < args.size(); ++i) {
        if (args.at(i) == "--delay") {
            bool ok = false;
            const int value = args.at(i + 1).toInt(&ok);
            if (ok && value >= 0) {
                return value;
            }
        }
    }
    return 3;
}

QString outputDir()
{
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QDir::current().filePath("cache");
    }
    const QString dir = QDir(base).filePath("capture-test");
    QDir().mkpath(dir);
    return dir;
}

QString saveImage(const QImage &image, const QString &dir, const QString &name)
{
    if (image.isNull()) {
        SPDLOG_WARN("{}: image is null", name);
        return {};
    }

    const QString path = QDir(dir).filePath(name);
    if (!image.save(path, "PNG")) {
        SPDLOG_WARN("{}: failed to save {}", name, path);
        return {};
    }

    SPDLOG_INFO("{}: saved {} ({}x{})", name, path, image.width(),
                image.height());
    return path;
}

QString windowText(HWND hwnd)
{
    wchar_t buffer[512] = {};
    GetWindowTextW(hwnd, buffer, static_cast<int>(_countof(buffer)));
    return QString::fromWCharArray(buffer);
}

QString className(HWND hwnd)
{
    wchar_t buffer[256] = {};
    GetClassNameW(hwnd, buffer, static_cast<int>(_countof(buffer)));
    return QString::fromWCharArray(buffer);
}

CaptureTarget foregroundTarget()
{
    CaptureTarget target;
    target.hwnd = GetForegroundWindow();
    if (!target.hwnd) {
        return target;
    }

    GetWindowRect(target.hwnd, &target.windowRect);
    target.windowTitle = windowText(target.hwnd);
    target.className = className(target.hwnd);
    return target;
}

QImage imageFromHbitmap(HBITMAP bitmap, int width, int height)
{
    if (!bitmap || width <= 0 || height <= 0) {
        return {};
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    QImage image(width, height, QImage::Format_ARGB32);
    HDC screenDc = GetDC(nullptr);
    const int lines = GetDIBits(screenDc, bitmap, 0, static_cast<UINT>(height),
                                image.bits(), &info, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screenDc);

    if (lines == 0) {
        return {};
    }
    return image;
}

QImage captureWithPrintWindow(HWND hwnd)
{
    RECT rect = {};
    if (!hwnd || !GetWindowRect(hwnd, &rect)) {
        return {};
    }

    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    HDC windowDc = GetWindowDC(hwnd);
    HDC memoryDc = CreateCompatibleDC(windowDc);
    HBITMAP bitmap = CreateCompatibleBitmap(windowDc, width, height);
    HGDIOBJ old = SelectObject(memoryDc, bitmap);

    const BOOL ok = PrintWindow(hwnd, memoryDc, PW_RENDERFULLCONTENT);
    QImage image = ok ? imageFromHbitmap(bitmap, width, height) : QImage();

    SelectObject(memoryDc, old);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(hwnd, windowDc);
    SPDLOG_INFO("PrintWindow returned {}", ok ? "true" : "false");
    return image;
}

QImage captureWindowFromDesktop(const RECT &rect)
{
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        return {};
    }

    HDC desktopDc = GetDC(nullptr);
    HDC memoryDc = CreateCompatibleDC(desktopDc);
    HBITMAP bitmap = CreateCompatibleBitmap(desktopDc, width, height);
    HGDIOBJ old = SelectObject(memoryDc, bitmap);
    const BOOL ok = BitBlt(memoryDc, 0, 0, width, height, desktopDc, rect.left,
                           rect.top, SRCCOPY | CAPTUREBLT);
    QImage image = ok ? imageFromHbitmap(bitmap, width, height) : QImage();

    SelectObject(memoryDc, old);
    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, desktopDc);
    SPDLOG_INFO("Desktop BitBlt returned {}", ok ? "true" : "false");
    return image;
}

QScreen *screenForWindow(HWND hwnd)
{
    if (!hwnd) {
        return QGuiApplication::primaryScreen();
    }

    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        const QString name = QString::fromWCharArray(info.szDevice);
        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen &&
                screen->name().compare(name, Qt::CaseInsensitive) == 0)
            {
                return screen;
            }
        }
        SPDLOG_WARN("No Qt screen matched monitor {}", name);
    }

    return QGuiApplication::screenAt(QCursor::pos())
               ? QGuiApplication::screenAt(QCursor::pos())
               : QGuiApplication::primaryScreen();
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    spdlog::set_level(spdlog::level::debug);

    const int delaySeconds = parseDelaySeconds(app.arguments());
    SPDLOG_INFO("OCR capture test will capture foreground window in {} "
                "seconds. Focus the target window now.",
                delaySeconds);
    QThread::sleep(static_cast<unsigned long>(delaySeconds));

    const CaptureTarget target = foregroundTarget();
    if (!target.hwnd) {
        SPDLOG_ERROR("No foreground window");
        return 1;
    }

    const int width = target.windowRect.right - target.windowRect.left;
    const int height = target.windowRect.bottom - target.windowRect.top;
    SPDLOG_INFO("Foreground hwnd={} class='{}' title='{}' rect=({}, {}, {}x{})",
                reinterpret_cast<void *>(target.hwnd), target.className,
                target.windowTitle, target.windowRect.left,
                target.windowRect.top, width, height);

    QScreen *screen = screenForWindow(target.hwnd);
    if (!screen) {
        SPDLOG_ERROR("No screen available");
        return 1;
    }
    SPDLOG_INFO("Qt screen='{}' geometry=({}, {}, {}x{}) dpr={}",
                screen->name(), screen->geometry().x(), screen->geometry().y(),
                screen->geometry().width(), screen->geometry().height(),
                screen->devicePixelRatio());

    const QString dir = outputDir();
    const QString stamp =
        QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz");

    saveImage(screen->grabWindow(reinterpret_cast<WId>(target.hwnd)).toImage(),
              dir, QString("qt-window-%1.png").arg(stamp));
    saveImage(screen->grabWindow(0).toImage(), dir,
              QString("qt-screen-%1.png").arg(stamp));
    saveImage(captureWithPrintWindow(target.hwnd), dir,
              QString("win32-printwindow-%1.png").arg(stamp));
    saveImage(captureWindowFromDesktop(target.windowRect), dir,
              QString("win32-desktop-crop-%1.png").arg(stamp));

    SPDLOG_INFO("Capture test output dir: {}", dir);
    return 0;
}
