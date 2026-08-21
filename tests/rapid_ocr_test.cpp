/// Standalone example for the RapidOcrOnnx C API (rapidocr-onnx-c-api).
///
/// The clib statically bundles onnxruntime + opencv, so only the
/// RapidOcrOnnx.dll needs to sit next to the executable.
///
/// Usage:
///   TalkInputRapidOcrTest <image-file> [models-dir]
///
/// Examples:
///   TalkInputRapidOcrTest C:\shot.png
///   TalkInputRapidOcrTest C:\shot.png C:\AppSource\TalkInput\third_parties\rapid-ocr\models
///
/// The models-dir defaults to the one compiled in via
/// TALKINPUT_RAPID_OCR_MODEL_DIR. Exit code is 0 when OCR ran and produced
/// text, 2 when no text boxes were found, 1 on error.

#include "OcrLiteCApi.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifndef TALKINPUT_RAPID_OCR_MODEL_DIR
#define TALKINPUT_RAPID_OCR_MODEL_DIR "third_parties/rapid-ocr/models"
#endif

namespace
{

std::string joinPath(const std::string &dir, const std::string &name)
{
    if (dir.empty()) {
        return name;
    }
    const char sep = (dir.find('\\') != std::string::npos) ? '\\' : '/';
    return dir.back() == sep || dir.back() == '/' || dir.back() == '\\'
               ? dir + name
               : dir + sep + name;
}

int run(const std::string &imagePath, const std::string &modelsDir)
{
    const std::string detModel = joinPath(modelsDir, "ch_PP-OCRv3_det_infer.onnx");
    const std::string clsModel = joinPath(modelsDir, "ch_ppocr_mobile_v2.0_cls_infer.onnx");
    const std::string recModel = joinPath(modelsDir, "ch_PP-OCRv3_rec_infer.onnx");
    const std::string keysPath = joinPath(modelsDir, "ppocr_keys_v1.txt");

    for (const auto &modelFile : {detModel, clsModel, recModel, keysPath}) {
        if (FILE *file = std::fopen(modelFile.c_str(), "rb")) {
            std::fclose(file);
        } else {
            std::fprintf(stderr, "FAIL: model not found: %s\n", modelFile.c_str());
            return 1;
        }
    }

    const int numThreads = static_cast<int>(std::thread::hardware_concurrency());
    printf("Initializing OCR (threads=%d)...\n", numThreads);
    OCR_HANDLE handle = OcrInit(detModel.c_str(), clsModel.c_str(),
                                recModel.c_str(), keysPath.c_str(), numThreads);
    if (!handle) {
        std::fprintf(stderr, "FAIL: OcrInit returned null\n");
        return 1;
    }

    OCR_PARAM param = {};
    param.padding = 50;
    param.maxSideLen = 1024;
    param.boxScoreThresh = 0.6f;
    param.boxThresh = 0.3f;
    param.unClipRatio = 2.0f;
    param.doAngle = 1;
    param.mostAngle = 1;

    printf("Detecting %s...\n", imagePath.c_str());
    const auto started = std::chrono::steady_clock::now();
    if (OcrDetect(handle, "", imagePath.c_str(), &param) == FALSE) {
        std::fprintf(stderr, "FAIL: OcrDetect failed\n");
        OcrDestroy(handle);
        return 1;
    }

    const int len = OcrGetLen(handle);
    std::vector<char> buffer(static_cast<size_t>(len > 0 ? len : 1), '\0');
    OcrGetResult(handle, buffer.data(), len);
    OcrDestroy(handle);

    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    std::string text(buffer.data());
    size_t lineCount = 0;
    while (!text.empty() && text.back() == '\n') {
        text.pop_back();
    }
    for (char ch : text) {
        if (ch == '\n') {
            ++lineCount;
        }
    }
    ++lineCount;

    printf("\n=== RapidOCR Result ===\n");
    printf("%s\n", text.empty() ? "(no text detected)" : text.c_str());
    printf("=== Done ===\n");
    printf("OCR took %lld ms, %zu text block(s), %zu chars\n",
           static_cast<long long>(ms), lineCount, text.size());

    return text.empty() ? 2 : 0;
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc < 2) {
        std::fprintf(stderr,
                     "Usage: TalkInputRapidOcrTest <image-file> [models-dir]\n"
                     "  e.g. TalkInputRapidOcrTest shot.png\n");
        return 1;
    }

    const std::string imagePath = argv[1];
    const std::string modelsDir = argc >= 3 ? argv[2] : TALKINPUT_RAPID_OCR_MODEL_DIR;
    return run(imagePath, modelsDir);
}