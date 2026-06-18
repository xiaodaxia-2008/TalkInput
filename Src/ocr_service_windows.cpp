#include "ocr_service_windows.h"

#include "logging.h"

#include <QBuffer>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <unknwn.h>
#include <windows.h>
#ifndef interface
#define interface struct
#endif
#include <UIAutomation.h>
// clang-format on
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/base.h>
#endif

namespace
{

#ifdef Q_OS_WIN
constexpr int MaxContextWidth = 900;
constexpr int MaxContextHeight = 360;

QRect rectFromWinRect(const RECT &rect)
{
    return QRect(QPoint(rect.left, rect.top), QPoint(rect.right, rect.bottom))
        .normalized();
}

QRect contextRectAround(const QRect &rect)
{
    if (rect.isEmpty()) {
        return {};
    }

    const QPoint center = rect.center();
    return QRect(center.x() - MaxContextWidth / 2,
                 center.y() - MaxContextHeight / 2, MaxContextWidth,
                 MaxContextHeight);
}

void initComApartment()
{
    thread_local bool initialized = false;
    if (initialized) {
        return;
    }

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
        initialized = true;
    }
    else {
        spdlog::warn("OCR: CoInitializeEx failed: 0x{:08x}",
                     static_cast<unsigned>(hr));
    }
}

template <typename T>
void releaseCom(T *&ptr)
{
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

HWND focusedWindowFromUiAutomation()
{
    initComApartment();

    IUIAutomation *automation = nullptr;
    HRESULT hr =
        CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&automation));
    if (FAILED(hr) || !automation) {
        spdlog::debug("OCR: UI Automation unavailable: 0x{:08x}",
                      static_cast<unsigned>(hr));
        return nullptr;
    }

    IUIAutomationElement *element = nullptr;
    hr = automation->GetFocusedElement(&element);
    releaseCom(automation);
    if (FAILED(hr) || !element) {
        spdlog::debug("OCR: UI Automation focused element unavailable: "
                      "0x{:08x}",
                      static_cast<unsigned>(hr));
        return nullptr;
    }

    UIA_HWND nativeWindow = 0;
    hr = element->get_CurrentNativeWindowHandle(&nativeWindow);
    releaseCom(element);
    if (FAILED(hr) || nativeWindow == 0) {
        spdlog::debug("OCR: UI Automation focused element has no native "
                      "window: 0x{:08x}",
                      static_cast<unsigned>(hr));
        return nullptr;
    }

    HWND hwnd = reinterpret_cast<HWND>(nativeWindow);
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root) {
        hwnd = root;
    }
    spdlog::debug("OCR: using UI Automation focused native window for "
                  "screenshot");
    return hwnd;
}

HWND focusedWindowFromWin32()
{
    HWND hwnd = nullptr;
    const HWND foreground = GetForegroundWindow();
    if (foreground) {
        const DWORD threadId = GetWindowThreadProcessId(foreground, nullptr);
        GUITHREADINFO info = {};
        info.cbSize = sizeof(info);
        if (GetGUIThreadInfo(threadId, &info) && info.hwndFocus) {
            hwnd = GetAncestor(info.hwndFocus, GA_ROOT);
            if (hwnd) {
                spdlog::debug("OCR: using Win32 focused root window for "
                              "screenshot");
            }
        }
    }

    if (!hwnd) {
        hwnd = foreground;
        if (hwnd) {
            spdlog::debug("OCR: using foreground window for screenshot");
        }
    }

    return hwnd;
}

HWND focusedInputWindow()
{
    HWND hwnd = focusedWindowFromUiAutomation();
    if (!hwnd) {
        hwnd = focusedWindowFromWin32();
    }
    return hwnd;
}

QRect focusedRectFromUiAutomation()
{
    initComApartment();

    IUIAutomation *automation = nullptr;
    HRESULT hr =
        CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&automation));
    if (FAILED(hr) || !automation) {
        spdlog::debug("OCR: UI Automation unavailable: 0x{:08x}",
                      static_cast<unsigned>(hr));
        return {};
    }

    IUIAutomationElement *element = nullptr;
    hr = automation->GetFocusedElement(&element);
    releaseCom(automation);
    if (FAILED(hr) || !element) {
        spdlog::debug("OCR: UI Automation focused element unavailable: "
                      "0x{:08x}",
                      static_cast<unsigned>(hr));
        return {};
    }

    RECT rect = {};
    hr = element->get_CurrentBoundingRectangle(&rect);
    releaseCom(element);
    if (FAILED(hr) || IsRectEmpty(&rect)) {
        spdlog::debug("OCR: UI Automation focused element has no bounds: "
                      "0x{:08x}",
                      static_cast<unsigned>(hr));
        return {};
    }

    spdlog::debug("OCR: using UI Automation focused element rect for context");
    return contextRectAround(rectFromWinRect(rect));
}

void initWinrtApartment()
{
    thread_local bool initialized = false;
    if (initialized) {
        return;
    }

    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    catch (const winrt::hresult_error &e) {
        if (e.code() != RPC_E_CHANGED_MODE) {
            throw;
        }
    }
    initialized = true;
}

QString recognizeWindowsText(QImage image)
{
    if (image.isNull()) {
        spdlog::debug("OCR: Windows OCR skipped empty image");
        return {};
    }

    initWinrtApartment();

    spdlog::debug("OCR: Windows OCR input image: {}x{}", image.width(),
                  image.height());
    if (image.width() > MaxContextWidth || image.height() > MaxContextHeight) {
        image = image.scaled(MaxContextWidth, MaxContextHeight,
                             Qt::KeepAspectRatio, Qt::SmoothTransformation);
        spdlog::debug("OCR: Windows OCR scaled image: {}x{}", image.width(),
                      image.height());
    }

    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        spdlog::warn("OCR: failed to encode screenshot as PNG");
        return {};
    }

    auto stream =
        winrt::Windows::Storage::Streams::InMemoryRandomAccessStream();
    auto writer = winrt::Windows::Storage::Streams::DataWriter(stream);
    const auto *begin = reinterpret_cast<const uint8_t *>(png.constData());
    writer.WriteBytes(winrt::array_view<const uint8_t>(
        begin, begin + static_cast<std::ptrdiff_t>(png.size())));
    writer.StoreAsync().get();
    writer.FlushAsync().get();
    writer.DetachStream();
    stream.Seek(0);

    const auto decoder =
        winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream)
            .get();
    const auto bitmap =
        decoder
            .GetSoftwareBitmapAsync(
                winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
                winrt::Windows::Graphics::Imaging::BitmapAlphaMode::
                    Premultiplied)
            .get();
    const auto engine = winrt::Windows::Media::Ocr::OcrEngine::
        TryCreateFromUserProfileLanguages();
    if (!engine) {
        spdlog::warn("OCR: Windows OCR engine is not available");
        return {};
    }

    const auto result = engine.RecognizeAsync(bitmap).get();
    const QString text =
        QString::fromStdWString(std::wstring(result.Text())).trimmed();
    spdlog::debug("OCR: Windows OCR result: {}", text);
    return text;
}
#endif

} // namespace

namespace talkinput
{

WindowsOcrService::WindowsOcrService(QObject *parent) : OcrService(parent)
{
}

bool WindowsOcrService::isAvailable() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

QRect WindowsOcrService::focusedTextInputRect() const
{
#ifdef Q_OS_WIN
    const QRect uiAutomationRect = focusedRectFromUiAutomation();
    if (!uiAutomationRect.isEmpty()) {
        return uiAutomationRect;
    }

    HWND foreground = GetForegroundWindow();
    if (!foreground) {
        spdlog::debug("OCR: no foreground window");
        return {};
    }

    const DWORD threadId = GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO info = {};
    info.cbSize = sizeof(info);
    if (GetGUIThreadInfo(threadId, &info)) {
        if (info.hwndCaret && !IsRectEmpty(&info.rcCaret)) {
            RECT caretRect = info.rcCaret;
            MapWindowPoints(info.hwndCaret, nullptr,
                            reinterpret_cast<POINT *>(&caretRect), 2);
            spdlog::debug("OCR: using caret rect for focused context");
            return contextRectAround(rectFromWinRect(caretRect));
        }

        if (info.hwndFocus) {
            RECT focusRect = {};
            if (GetWindowRect(info.hwndFocus, &focusRect) &&
                !IsRectEmpty(&focusRect))
            {
                spdlog::debug("OCR: using focused window rect for context");
                return contextRectAround(rectFromWinRect(focusRect));
            }
        }
    }

    spdlog::debug("OCR: no focused text rect; caller should use full-screen "
                  "fallback");
#endif

    return {};
}

WId WindowsOcrService::focusedTextInputWindowId() const
{
#ifdef Q_OS_WIN
    return reinterpret_cast<WId>(focusedInputWindow());
#else
    return 0;
#endif
}

QString WindowsOcrService::focusedTextInputScreenName() const
{
#ifdef Q_OS_WIN
    const HWND hwnd = focusedInputWindow();
    if (!hwnd) {
        return {};
    }

    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor) {
        return {};
    }

    MONITORINFOEXW info = {};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return {};
    }

    const QString name = QString::fromWCharArray(info.szDevice);
    spdlog::debug("OCR: focused input monitor: {}", name);
    return name;
#else
    return {};
#endif
}

void WindowsOcrService::recognizeText(const QImage &image, QObject *receiver,
                                      Callback callback)
{
    if (!receiver || !callback) {
        return;
    }

    const QImage imageCopy = image.copy();
    const QPointer<QObject> context(receiver);
    QThreadPool::globalInstance()->start(
        [imageCopy, context, callback = std::move(callback)]() mutable {
            QString text;
#ifdef Q_OS_WIN
            try {
                text = recognizeWindowsText(imageCopy);
            }
            catch (const winrt::hresult_error &e) {
                spdlog::warn("OCR: Windows OCR failed: {}",
                             winrt::to_string(e.message()));
            }
            catch (const std::exception &e) {
                spdlog::warn("OCR: Windows OCR failed: {}", e.what());
            }
#endif
            if (!context) {
                return;
            }
            QMetaObject::invokeMethod(
                context,
                [callback = std::move(callback),
                 text = std::move(text)]() mutable { callback(text); },
                Qt::QueuedConnection);
        });
}

} // namespace talkinput
