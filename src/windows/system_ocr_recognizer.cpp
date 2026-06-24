#include "../system_ocr_recognizer.h"

#include "../logging.h"
#include "../utils.h"

#include <QBuffer>
#include <QCoro/QCoroFuture>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QPromise>
#include <QStandardPaths>
#include <QThreadPool>
#include <spdlog/stopwatch.h>

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

namespace
{

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

QImage captureWindowWithPrintWindow(HWND hwnd)
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

    return image;
}

QImage captureWindowFromDesktop(HWND hwnd)
{
    RECT rect = {};
    if (!hwnd || !GetWindowRect(hwnd, &rect)) {
        return {};
    }

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

    return image;
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
        SPDLOG_WARN("OCR: CoInitializeEx failed: 0x{:08x}",
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
        return nullptr;
    }

    IUIAutomationElement *element = nullptr;
    hr = automation->GetFocusedElement(&element);
    releaseCom(automation);
    if (FAILED(hr) || !element) {
        return nullptr;
    }

    UIA_HWND nativeWindow = 0;
    hr = element->get_CurrentNativeWindowHandle(&nativeWindow);
    releaseCom(element);
    if (FAILED(hr) || nativeWindow == 0) {
        return nullptr;
    }

    HWND hwnd = reinterpret_cast<HWND>(nativeWindow);
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (root) {
        hwnd = root;
    }
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
            }
        }
    }

    if (!hwnd) {
        hwnd = foreground;
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

void initWinrtApartment()
{
    thread_local bool initialized = false;
    if (initialized) {
        return;
    }

    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
    }
    catch (const winrt::hresult_error &e) {
        if (e.code() != RPC_E_CHANGED_MODE) {
            throw;
        }
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    initialized = true;
}

QString recognizeWindowsText(QImage image)
{
    spdlog::stopwatch sw;
    if (image.isNull()) {
        return {};
    }

    initWinrtApartment();

    // NOTE: deliberately NOT scaling the image here. The OCR engine handles
    // large images well (~250ms for 2576x1456). Scaling to 900x360 made text
    // unreadable and caused the empty result bug. MaxContextWidth/Height are
    // used only for contextRectAround() in the focus-area logic, not for OCR.

    // Ensure we have a consistent pixel layout: convert to Format_RGB32
    // (byte-aligned 32-bit BGRA with unused alpha) so the BMP we write
    // and decode matches what the OCR engine expects.
    if (image.format() != QImage::Format_RGB32) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    // Write BMP to a temporary file and open it via StorageFile (same
    // approach as the test program which reliably works). The in-memory
    // DataWriter + InMemoryRandomAccessStream path produces valid decoder
    // output but OCR returns empty for unknown reasons.
    const QString tempPath =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath("talkinput-ocr.bmp");
    if (!image.save(tempPath, "BMP")) {
        return {};
    }

    winrt::Windows::Storage::StorageFile file = nullptr;
    try {
        // StorageFile::GetFileFromPathAsync needs Windows backslashes
        const auto nativePath =
            QDir::toNativeSeparators(tempPath).toStdWString();
        file = winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(
                   nativePath)
                   .get();
    }
    catch (const winrt::hresult_error &e) {
        SPDLOG_WARN("OCR: failed to open temp file: {}",
                    winrt::to_string(e.message()));
        QFile::remove(tempPath);
        return {};
    }

    const auto stream =
        file.OpenAsync(winrt::Windows::Storage::FileAccessMode::Read).get();
    const auto decoder =
        winrt::Windows::Graphics::Imaging::BitmapDecoder::CreateAsync(stream)
            .get();
    const auto bitmap =
        decoder
            .GetSoftwareBitmapAsync(
                winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
                winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Ignore)
            .get();

    // Clean up temp file
    QFile::remove(tempPath);

    // Try Chinese OCR first (zh-Hans-CN), then fall back to user profile
    // languages. The user's screenshot mostly contains Chinese text; with en-US
    // the OCR returns empty results because the language pack doesn't match the
    // text.
    auto engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(
        winrt::Windows::Globalization::Language(L"zh-Hans-CN"));
    if (!engine) {
        engine = winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(
            winrt::Windows::Globalization::Language(L"zh-Hans"));
    }
    if (!engine) {
        engine = winrt::Windows::Media::Ocr::OcrEngine::
            TryCreateFromUserProfileLanguages();
    }
    if (!engine) {
        SPDLOG_WARN("OCR: Windows OCR engine is not available");
        return {};
    }

    const auto result = engine.RecognizeAsync(bitmap).get();
    const auto textHstring = result.Text();
    const QString text =
        QString::fromStdWString(std::wstring(textHstring)).trimmed();
    SPDLOG_DEBUG("OCR: Windows OCR result: {}", text);
    SPDLOG_INFO("OCR: Windows OCR completed in {:.3}s (image {}x{})", sw,
                image.width(), image.height());

    return text;
}

} // namespace

namespace talkinput
{

SystemOcrRecognizer::SystemOcrRecognizer(QObject *parent)
    : OcrRecognizer(parent)
{
}

bool SystemOcrRecognizer::isAvailable() const
{
    return true;
}

QImage SystemOcrRecognizer::captureContextImage() const
{
    const HWND hwnd = focusedInputWindow();
    if (!hwnd) {
        return OcrRecognizer::captureContextImage();
    }

    // Desktop BitBlt first: reliable for all window types (including
    // DirectX-accelerated windows where PrintWindow returns blank).
    QImage image = captureWindowFromDesktop(hwnd);
    if (!image.isNull()) {
        return image;
    }

    // Fallback to PrintWindow for off-screen or obscured windows.
    image = captureWindowWithPrintWindow(hwnd);
    if (!image.isNull()) {
        return image;
    }

    return OcrRecognizer::captureContextImage();
}

QCoro::Task<QString> SystemOcrRecognizer::recognizeText(const QImage &image)
{
    QPromise<QString> promise;
    promise.start();
    auto future = promise.future();

    const QImage imageCopy = image.copy();
    QThreadPool::globalInstance()->start(
        [imageCopy, promise = std::move(promise)]() mutable {
            QString text;
            try {
                text = recognizeWindowsText(imageCopy);
            }
            catch (const winrt::hresult_error &e) {
                SPDLOG_WARN("OCR: Windows OCR failed: {}",
                            winrt::to_string(e.message()));
            }
            catch (const std::exception &e) {
                SPDLOG_WARN("OCR: Windows OCR failed: {}", e.what());
            }
            promise.addResult(text);
            promise.finish();
        });

    co_return co_await future;
}

} // namespace talkinput
