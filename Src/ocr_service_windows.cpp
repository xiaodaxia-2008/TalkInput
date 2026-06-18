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
#include <unknwn.h>
#include <windows.h>
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

    RECT windowRect = {};
    if (GetWindowRect(foreground, &windowRect) && !IsRectEmpty(&windowRect)) {
        spdlog::debug("OCR: using foreground window rect for context");
        return contextRectAround(rectFromWinRect(windowRect));
    }
#endif

    return {};
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
