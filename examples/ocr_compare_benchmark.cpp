/// OCR comparison: System (WinRT) vs Tesseract on the same image.
///
/// Usage:
///   TalkInputOcrCompareTest <image-file> [tessdata-dir]
///
/// Both engines are timed end-to-end (init + infer) and their text
/// outputs are printed. The image defaults to data/images/paseo_sreenshot.png
/// when no argument is given.

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.h>
#include <winrt/base.h>

#ifndef TALKINPUT_TESSERACT_TRAIN_DATA_DIR
#define TALKINPUT_TESSERACT_TRAIN_DATA_DIR "teact/train_data"
#endif

using namespace winrt;
using namespace Windows::Media::Ocr;
using namespace Windows::Graphics::Imaging;
using namespace Windows::Storage;

struct BenchResult {
    std::string engine;
    long long initMs = -1;
    long long inferMs = -1;
    std::string text;
    bool ok = false;
    std::string error;
};

static std::string wstringToUtf8(const std::wstring &ws)
{
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), static_cast<int>(ws.size()), out.data(), len, nullptr, nullptr);
    return out;
}

static std::string joinPath(const std::string &dir, const std::string &name)
{
    if (dir.empty()) return name;
    const char sep = (dir.find('\\') != std::string::npos) ? '\\' : '/';
    return dir.back() == sep || dir.back() == '/' || dir.back() == '\\' ? dir + name : dir + sep + name;
}

// ---------------------------------------------------------------------------
// System OCR (WinRT)
// ---------------------------------------------------------------------------
BenchResult benchSystemOcr(const std::string &imagePath)
{
    BenchResult r;
    r.engine = "System (WinRT)";

    // Init engine
    auto t0 = std::chrono::steady_clock::now();
    OcrEngine engine = nullptr;
    try {
        // Prefer zh-Hans-CN for the paseo screenshot (mixed Chinese/English)
        engine = OcrEngine::TryCreateFromLanguage(Windows::Globalization::Language(L"zh-Hans-CN"));
        if (!engine) engine = OcrEngine::TryCreateFromLanguage(Windows::Globalization::Language(L"zh-Hans"));
        if (!engine) engine = OcrEngine::TryCreateFromUserProfileLanguages();
    } catch (const hresult_error &e) {
        r.error = "TryCreateFromLanguage failed: " + to_string(e.message());
        return r;
    }
    auto t1 = std::chrono::steady_clock::now();
    r.initMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    if (!engine) {
        r.error = "No OCR engine available (install language pack)";
        return r;
    }

    // Load image via StorageFile -> SoftwareBitmap, then Recognize
    try {
        std::wstring wpath;
        {
            // StorageFile needs absolute native path
            QFileInfo fi(QString::fromStdString(imagePath));
            wpath = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdWString();
        }

        auto t2 = std::chrono::steady_clock::now();
        StorageFile file = StorageFile::GetFileFromPathAsync(wpath).get();
        auto stream = file.OpenAsync(FileAccessMode::Read).get();
        auto decoder = BitmapDecoder::CreateAsync(stream).get();
        SoftwareBitmap bitmap = decoder.GetSoftwareBitmapAsync(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Ignore).get();
        OcrResult ocrResult = engine.RecognizeAsync(bitmap).get();
        auto t3 = std::chrono::steady_clock::now();
        r.inferMs = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

        auto htext = ocrResult.Text();
        r.text = wstringToUtf8(std::wstring(htext.c_str(), htext.size()));
        // Trim
        while (!r.text.empty() && (r.text.back() == '\n' || r.text.back() == '\r' || r.text.back() == ' ')) r.text.pop_back();
        r.ok = true;
    } catch (const hresult_error &e) {
        r.error = "WinRT error: " + to_string(e.message()) + " (0x" + std::to_string((unsigned)e.code()) + ")";
    } catch (const std::exception &e) {
        r.error = e.what();
    }
    return r;
}

// ---------------------------------------------------------------------------
// Tesseract
// ---------------------------------------------------------------------------
std::string findTessdataDir(const std::string &hint)
{
    if (!hint.empty() && QFileInfo::exists(QString::fromStdString(joinPath(hint, "chi_sim.traineddata")))) {
        return hint;
    }
    // 1) exe dir /tessdata (TalkInput symlink)
    QString exeDir = QCoreApplication::applicationDirPath();
    QString p = exeDir + "/tessdata";
    if (QFileInfo::exists(p + "/chi_sim.traineddata")) return p.toStdString();
    // 2) exe dir itself (some layouts)
    if (QFileInfo::exists(exeDir + "/chi_sim.traineddata")) return exeDir.toStdString();
    // 3) compile-time default
    QString def = QString::fromUtf8(TALKINPUT_TESSERACT_TRAIN_DATA_DIR);
    if (QFileInfo::exists(def + "/chi_sim.traineddata")) return def.toStdString();
    // 4) project root teact/train_data
    QString proj = QDir(exeDir).filePath("../../teact/train_data");
    if (QFileInfo::exists(proj + "/chi_sim.traineddata")) return QDir::cleanPath(proj).toStdString();
    return hint.empty() ? def.toStdString() : hint;
}

BenchResult benchTesseract(const std::string &imagePath, const std::string &tessdataHint)
{
    BenchResult r;
    r.engine = "Tesseract";

    std::string tessdataDir = findTessdataDir(tessdataHint);
    if (!QFileInfo::exists(QString::fromStdString(joinPath(tessdataDir, "chi_sim.traineddata")))) {
        r.error = "chi_sim.traineddata not found (tried " + tessdataDir + ")";
        return r;
    }

    tesseract::TessBaseAPI api;
    auto t0 = std::chrono::steady_clock::now();
    if (api.Init(tessdataDir.c_str(), "chi_sim") != 0) {
        r.error = "TessBaseAPI::Init failed with tessdataDir=" + tessdataDir;
        return r;
    }
    api.SetPageSegMode(tesseract::PSM_AUTO);
    auto t1 = std::chrono::steady_clock::now();
    r.initMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    QImage image(QString::fromStdString(imagePath));
    if (image.isNull()) {
        r.error = "QImage::load failed: " + imagePath;
        api.End();
        return r;
    }

    // Convert QImage -> Pix (same as TesseractOcrRecognizer)
    QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    int w = rgb.width();
    int h = rgb.height();
    Pix *pix = pixCreate(w, h, 32);
    if (!pix) {
        r.error = "pixCreate failed";
        api.End();
        return r;
    }
    for (int y = 0; y < h; ++y) {
        const auto *src = rgb.constScanLine(y);
        l_uint32 *dst = pixGetData(pix) + y * pixGetWpl(pix);
        for (int x = 0; x < w; ++x) {
            dst[x] = (static_cast<l_uint32>(src[x * 4 + 2]) << 24) |
                     (static_cast<l_uint32>(src[x * 4 + 1]) << 16) |
                     (static_cast<l_uint32>(src[x * 4 + 0]) << 8) | 0xFF;
        }
    }

    auto t2 = std::chrono::steady_clock::now();
    api.SetImage(pix);
    char *utf8 = api.GetUTF8Text();
    auto t3 = std::chrono::steady_clock::now();
    r.inferMs = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

    if (utf8) {
        r.text = utf8;
        delete[] utf8;
        // Trim
        while (!r.text.empty() && (r.text.back() == '\n' || r.text.back() == '\r' || r.text.back() == ' ')) r.text.pop_back();
        size_t start = 0;
        while (start < r.text.size() && (r.text[start] == '\n' || r.text[start] == '\r' || r.text[start] == ' ')) ++start;
        if (start) r.text = r.text.substr(start);
        r.ok = true;
    } else {
        r.error = "GetUTF8Text returned null";
    }

    api.Clear();
    pixDestroy(&pix);
    api.End();
    return r;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    // Ensure console can show UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    QCoreApplication app(argc, argv);

    std::string imagePath = "data/images/paseo_sreenshot.png";
    std::string tessdataDir = TALKINPUT_TESSERACT_TRAIN_DATA_DIR;

    if (argc >= 2) imagePath = argv[1];
    if (argc >= 3) tessdataDir = argv[2];

    // Resolve image to absolute for WinRT StorageFile
    QFileInfo fi(QString::fromStdString(imagePath));
    if (!fi.exists()) {
        std::fprintf(stderr, "Image not found: %s\n", imagePath.c_str());
        return 1;
    }
    std::string absImage = QDir::toNativeSeparators(fi.absoluteFilePath()).toStdString();
    // For Tesseract, keep native separators; use absolute for consistency
    std::string absImageNorm = fi.absoluteFilePath().toStdString();

    // WinRT apartment (needed for System OCR)
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
        // already initialized
    }

    QImage qimg(QString::fromStdString(absImageNorm));
    printf("Image: %s  (%dx%d)  %.1f KB\n", absImage.c_str(), qimg.width(), qimg.height(), fi.size() / 1024.0);
    printf("Tessdata: %s\n\n", tessdataDir.c_str());

    std::vector<BenchResult> results;
    results.push_back(benchSystemOcr(absImage));
    results.push_back(benchTesseract(absImageNorm, tessdataDir));

    // Print table
    printf("\n%-18s | %8s | %8s | %8s | %6s | %s\n", "Engine", "init ms", "infer ms", "total ms", "chars", "status");
    printf("-------------------+----------+----------+----------+--------+----------------\n");
    for (auto &r : results) {
        long long total = (r.initMs >= 0 && r.inferMs >= 0) ? r.initMs + r.inferMs : -1;
        const char *status = r.ok ? "OK" : r.error.c_str();
        // Truncate status for table
        std::string shortStatus = status;
        if (shortStatus.size() > 40) shortStatus = shortStatus.substr(0, 37) + "...";
        printf("%-18s | %8lld | %8lld | %8lld | %6zu | %s\n",
               r.engine.c_str(), r.initMs, r.inferMs, total, r.text.size(), shortStatus.c_str());
    }

    // Print text snippets
    for (auto &r : results) {
        printf("\n===== %s (%s) =====\n", r.engine.c_str(), r.ok ? "OK" : "FAIL");
        if (r.ok) {
            // Print first 800 chars, indicate truncation
            std::string out = r.text;
            if (out.size() > 800) out = out.substr(0, 800) + "\n... (truncated, total " + std::to_string(r.text.size()) + " chars)";
            printf("%s\n", out.c_str());
            if (out.empty()) printf("(empty)\n");
        } else {
            printf("ERROR: %s\n", r.error.c_str());
        }
    }

    return 0;
}
