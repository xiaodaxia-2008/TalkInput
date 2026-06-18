/// Standalone test: loads an image and runs Windows.Media.Ocr on it.
///
/// Usage:
///   TalkInputOcrRecognitionTest <image-file> [language-tag]
///
/// Examples:
///   TalkInputOcrRecognitionTest C:\screenshot.png
///   TalkInputOcrRecognitionTest C:\screenshot.png zh-Hans-CN
///   TalkInputOcrRecognitionTest C:\test.png en-US
///
/// If language-tag is omitted, TryCreateFromUserProfileLanguages() is used.
/// If the specified language is unsupported, the test falls back to the
/// system-configured OCR language.

#include <iostream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Storage.h>
#include <winrt/base.h>

#pragma comment(lib, "windowsapp")

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Globalization;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Media::Ocr;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;

// ---------------------------------------------------------------------------
// Coroutine: load image from disk, run OCR, print results
// ---------------------------------------------------------------------------
IAsyncAction RunOcrAsync(std::wstring imagePath, std::wstring languageTag)
{
    // 1. Load file
    StorageFile file = nullptr;
    try {
        file = co_await StorageFile::GetFileFromPathAsync(imagePath);
    }
    catch (const hresult_error &e) {
        std::wcerr << L"FAIL: cannot open file \u2013 " << e.message().c_str()
                   << std::endl;
        co_return;
    }

    // 2. Decode bitmap (Bgra8 + Ignore alpha, matching the production fix)
    SoftwareBitmap bitmap = nullptr;
    try {
        auto stream = co_await file.OpenAsync(FileAccessMode::Read);
        auto decoder = co_await BitmapDecoder::CreateAsync(stream);
        bitmap = co_await decoder.GetSoftwareBitmapAsync(
            BitmapPixelFormat::Bgra8, BitmapAlphaMode::Ignore);
    }
    catch (const hresult_error &e) {
        std::wcerr << L"FAIL: cannot decode image \u2013 "
                   << e.message().c_str() << std::endl;
        co_return;
    }

    // 3. Create OCR engine
    OcrEngine engine = nullptr;
    if (!languageTag.empty()) {
        Language lang(languageTag);
        if (OcrEngine::IsLanguageSupported(lang)) {
            engine = OcrEngine::TryCreateFromLanguage(lang);
            std::wcout << L"Language: " << languageTag << L" (explicit)"
                       << std::endl;
        }
        else {
            std::wcerr << L"WARN: language '" << languageTag
                       << L"' is not installed. Falling back to user "
                          L"profile languages."
                       << std::endl;
        }
    }

    if (!engine) {
        engine = OcrEngine::TryCreateFromUserProfileLanguages();
        if (engine) {
            std::wcout << L"Language: "
                       << engine.RecognizerLanguage().LanguageTag().c_str()
                       << L" (user profile)" << std::endl;
        }
    }

    if (!engine) {
        std::wcerr << L"FAIL: no OCR engine available. Install a Windows OCR "
                      L"language pack (Settings \u2192 Time & Language \u2192 "
                      L"Language \u2192 Add a language)."
                   << std::endl;
        co_return;
    }

    // 4. Run recognition, measuring time
    std::wcout << L"Image: " << imagePath << L" (" << bitmap.PixelWidth()
               << L"x" << bitmap.PixelHeight() << L")" << std::endl;

    OcrResult result = nullptr;
    {
        auto start = std::chrono::steady_clock::now();
        result = co_await engine.RecognizeAsync(bitmap);
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                      .count();
        std::wcout << L"OCR took " << ms << L" ms" << std::endl;
    }

    // 5. Print the full recognized text
    // Note: per-line/per-word iteration via IVectorView<OcrLine> requires
    // full type resolution that is not available with this SDK's C++/WinRT
    // header ordering. Use result.Text() which returns hstring directly.
    std::wcout << L"\n=== OCR Result ===" << std::endl;
    const auto text = result.Text();
    if (text.empty()) {
        std::wcout << L"(no text detected)" << std::endl;
    }
    else {
        std::wcout << text.c_str() << std::endl;
    }
    std::wcout << L"=== Done ===" << std::endl;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int wmain(int argc, wchar_t *argv[])
{
    if (argc < 2) {
        std::wcerr << L"Usage: TalkInputOcrRecognitionTest <image-file> "
                      L"[language-tag]\n"
                      L"  e.g. TalkInputOcrRecognitionTest test.png "
                      L"zh-Hans-CN"
                   << std::endl;
        return 1;
    }

    const std::wstring imagePath = argv[1];
    const std::wstring languageTag = (argc >= 3) ? argv[2] : L"";

    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        RunOcrAsync(imagePath, languageTag).get();
    }
    catch (const hresult_error &e) {
        std::wcerr << L"FATAL: " << e.message().c_str() << L" (0x" << std::hex
                   << e.code() << L")" << std::endl;
        return 1;
    }
    catch (const std::exception &e) {
        std::wcerr << L"FATAL: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
