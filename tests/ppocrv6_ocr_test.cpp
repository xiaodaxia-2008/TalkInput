/// PP-OCRv6 medium end-to-end OCR test using the prebuilt ONNX Runtime.
///
/// The test runs DB text detection, crops the detected text lines, runs the
/// CTC recognition model, decodes the PP-OCRv6 dictionary, and prints the
/// final text. It also keeps model-only timings for comparison.
///
/// Usage:
///   TalkInputPpOcrV6Test [image] [det.onnx] [rec.onnx] [keys.txt]

#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef HAVE_OPENCV
#include <clipper2/clipper.h>
#include <opencv2/core.hpp>
#include <opencv2/geometry/2d.hpp>
#include <opencv2/imgproc.hpp>
#endif

#ifndef TALKINPUT_PPOCRV6_MODEL_DIR
#define TALKINPUT_PPOCRV6_MODEL_DIR                                            \
    "C:/Users/xiaoz/AppData/Local/Temp/opencode/ppocrv6_medium"
#endif

static std::string findModel(const std::string &hint,
                             const std::string &fallback)
{
    if (QFileInfo::exists(QString::fromStdString(hint))) {
        return hint;
    }
    if (QFileInfo::exists(QString::fromStdString(fallback))) {
        return fallback;
    }
    return hint;
}

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>

namespace
{

struct TextBox
{
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    float detectionScore = 0.0F;
    float recognitionScore = 0.0F;
    std::string text;
#ifdef HAVE_OPENCV
    std::array<cv::Point2f, 4> quad{};
#endif
};

struct RecognitionResult
{
    std::string text;
    float score = 0.0F;
};

std::wstring toWstring(const std::string &value)
{
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), length);
    if (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

Ort::SessionOptions sessionOptions()
{
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(4);
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    return options;
}

Ort::Value runModel(Ort::Session &session, const std::vector<float> &input,
                    const std::vector<int64_t> &shape)
{
    Ort::AllocatorWithDefaultOptions allocator;
    auto inputName = session.GetInputNameAllocated(0, allocator);
    auto outputName = session.GetOutputNameAllocated(0, allocator);
    const char *inputNames[] = {inputName.get()};
    const char *outputNames[] = {outputName.get()};
    Ort::MemoryInfo memory =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(
        memory, const_cast<float *>(input.data()), input.size(), shape.data(),
        shape.size());
    auto output = session.Run(Ort::RunOptions{nullptr}, inputNames, &tensor, 1,
                              outputNames, 1);
    return std::move(output.front());
}

std::vector<float> detectorInput(const QImage &source, int height, int width)
{
    const QImage image = source.convertToFormat(QImage::Format_RGB888)
                             .scaled(width, height, Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
    std::vector<float> result(static_cast<size_t>(3) * height * width);
    for (int y = 0; y < height; ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const size_t offset = static_cast<size_t>(y) * width + x;
            // PP-OCRv6 det uses BGR data with mean=0.5 and std=0.5.
            result[offset] = line[x * 3 + 2] / 127.5F - 1.0F;
            result[static_cast<size_t>(height) * width + offset] =
                line[x * 3 + 1] / 127.5F - 1.0F;
            result[static_cast<size_t>(2) * height * width + offset] =
                line[x * 3] / 127.5F - 1.0F;
        }
    }
    return result;
}

std::vector<float> recognizerInput(const QImage &source)
{
    constexpr int height = 48;
    constexpr int width = 320;
    const QImage rgb = source.convertToFormat(QImage::Format_RGB888);
    const int resizedWidth =
        std::clamp(static_cast<int>(std::ceil(
                       rgb.width() * height /
                       static_cast<double>(std::max(1, rgb.height())))),
                   1, width);
    const QImage image = rgb.scaled(resizedWidth, height, Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation);
    std::vector<float> result(static_cast<size_t>(3) * height * width, 0.0F);
    for (int y = 0; y < height; ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < resizedWidth; ++x) {
            const size_t offset = static_cast<size_t>(y) * width + x;
            // RecResizeImg uses mean=0.5 and std=0.5 for each channel.
            result[offset] = line[x * 3 + 2] / 127.5F - 1.0F;
            result[static_cast<size_t>(height) * width + offset] =
                line[x * 3 + 1] / 127.5F - 1.0F;
            result[static_cast<size_t>(2) * height * width + offset] =
                line[x * 3] / 127.5F - 1.0F;
        }
    }
    return result;
}

#ifdef HAVE_OPENCV

cv::Mat qimageToRgbMat(const QImage &source)
{
    const QImage rgb = source.convertToFormat(QImage::Format_RGB888);
    return cv::Mat(rgb.height(), rgb.width(), CV_8UC3,
                   const_cast<uchar *>(rgb.constBits()), rgb.bytesPerLine())
        .clone();
}

std::vector<float> recognizerInput(const cv::Mat &source)
{
    constexpr int height = 48;
    constexpr int width = 320;
    if (source.empty() || source.channels() != 3) {
        throw std::runtime_error("invalid OpenCV recognition crop");
    }

    const int resizedWidth =
        std::clamp(static_cast<int>(std::ceil(
                       source.cols * height /
                       static_cast<double>(std::max(1, source.rows)))),
                   1, width);
    cv::Mat image;
    cv::resize(source, image, cv::Size(resizedWidth, height), 0.0, 0.0,
               cv::INTER_LINEAR);

    std::vector<float> result(static_cast<size_t>(3) * height * width, 0.0F);
    for (int y = 0; y < height; ++y) {
        const auto *line = image.ptr<cv::Vec3b>(y);
        for (int x = 0; x < resizedWidth; ++x) {
            const size_t offset = static_cast<size_t>(y) * width + x;
            // OpenCV mat is RGB, while PP-OCR rec expects BGR planes.
            result[offset] = line[x][2] / 127.5F - 1.0F;
            result[static_cast<size_t>(height) * width + offset] =
                line[x][1] / 127.5F - 1.0F;
            result[static_cast<size_t>(2) * height * width + offset] =
                line[x][0] / 127.5F - 1.0F;
        }
    }
    return result;
}

std::array<cv::Point2f, 4> orderedQuad(const cv::RotatedRect &rect)
{
    cv::Point2f points[4];
    rect.points(points);
    std::sort(std::begin(points), std::end(points),
              [](const cv::Point2f &a, const cv::Point2f &b) {
                  return a.y < b.y || (a.y == b.y && a.x < b.x);
              });
    std::array<cv::Point2f, 4> result;
    if (points[0].x <= points[1].x) {
        result[0] = points[0];
        result[1] = points[1];
    }
    else {
        result[0] = points[1];
        result[1] = points[0];
    }
    if (points[2].x >= points[3].x) {
        result[2] = points[2];
        result[3] = points[3];
    }
    else {
        result[2] = points[3];
        result[3] = points[2];
    }
    return result;
}

double clipperPathArea(const Clipper2Lib::PathD &path)
{
    double area = 0.0;
    for (size_t i = 0; i < path.size(); ++i) {
        const auto &current = path[i];
        const auto &next = path[(i + 1) % path.size()];
        area += current.x * next.y - next.x * current.y;
    }
    return std::abs(area) * 0.5;
}

std::optional<cv::RotatedRect>
unclipContour(const std::vector<cv::Point> &contour, double distance)
{
    Clipper2Lib::PathD path;
    path.reserve(contour.size());
    for (const auto &point : contour) {
        path.emplace_back(static_cast<double>(point.x),
                          static_cast<double>(point.y));
    }

    const auto expandedPaths = Clipper2Lib::InflatePaths(
        {path}, distance, Clipper2Lib::JoinType::Round,
        Clipper2Lib::EndType::Polygon);
    if (expandedPaths.empty()) {
        return std::nullopt;
    }

    const auto largest = std::max_element(
        expandedPaths.begin(), expandedPaths.end(),
        [](const Clipper2Lib::PathD &left, const Clipper2Lib::PathD &right) {
            return clipperPathArea(left) < clipperPathArea(right);
        });
    std::vector<cv::Point2f> points;
    points.reserve(largest->size());
    for (const auto &point : *largest) {
        points.emplace_back(static_cast<float>(point.x),
                            static_cast<float>(point.y));
    }
    if (points.size() < 3) {
        return std::nullopt;
    }
    return cv::minAreaRect(points);
}

cv::Mat perspectiveTextCrop(const cv::Mat &image,
                            const std::array<cv::Point2f, 4> &quad)
{
    const float topWidth = cv::norm(quad[1] - quad[0]);
    const float bottomWidth = cv::norm(quad[2] - quad[3]);
    const float leftHeight = cv::norm(quad[3] - quad[0]);
    const float rightHeight = cv::norm(quad[2] - quad[1]);
    const int width =
        std::clamp(static_cast<int>(std::round(
                       std::max(topWidth, bottomWidth) * 48.0F /
                       std::max(1.0F, std::max(leftHeight, rightHeight)))),
                   8, 320);
    const std::array<cv::Point2f, 4> destination = {
        cv::Point2f(0.0F, 0.0F),
        cv::Point2f(static_cast<float>(width - 1), 0.0F),
        cv::Point2f(static_cast<float>(width - 1), 47.0F),
        cv::Point2f(0.0F, 47.0F)};
    const cv::Mat transform =
        cv::getPerspectiveTransform(quad.data(), destination.data());
    cv::Mat crop;
    cv::warpPerspective(image, crop, transform, cv::Size(width, 48),
                        cv::INTER_CUBIC, cv::BORDER_REPLICATE);
    return crop;
}

#endif

std::vector<std::string> loadDictionary(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open dictionary: " + path);
    }

    std::vector<std::string> dictionary;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        dictionary.push_back(std::move(line));
    }
    return dictionary;
}

RecognitionResult decodeRecognition(const Ort::Value &output,
                                    const std::vector<std::string> &dictionary)
{
    const auto shape = output.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 3 || shape[0] != 1 || shape[2] <= 1) {
        throw std::runtime_error("unexpected recognition output shape");
    }

    const int64_t steps = shape[1];
    const int64_t classes = shape[2];
    const float *values = output.GetTensorData<float>();
    RecognitionResult result;
    int64_t previous = -1;
    float selectedScore = 0.0F;
    int selectedCount = 0;
    for (int64_t step = 0; step < steps; ++step) {
        const float *row = values + step * classes;
        const auto best = std::max_element(row, row + classes);
        const int64_t index = std::distance(row, best);
        if (index != previous && index > 0 &&
            index <= static_cast<int64_t>(dictionary.size()) + 1)
        {
            if (index == static_cast<int64_t>(dictionary.size()) + 1) {
                result.text += ' ';
            }
            else {
                result.text += dictionary[static_cast<size_t>(index - 1)];
            }
            selectedScore += *best;
            ++selectedCount;
        }
        previous = index;
    }
    result.score = selectedCount == 0 ? 0.0F : selectedScore / selectedCount;
    return result;
}

std::vector<TextBox> detectText(Ort::Session &session, const QImage &image,
                                int inputHeight, int inputWidth)
{
    const auto input = detectorInput(image, inputHeight, inputWidth);
    const Ort::Value output =
        runModel(session, input, {1, 3, inputHeight, inputWidth});
    const auto shape = output.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 4 || shape[0] != 1 || shape[1] != 1) {
        throw std::runtime_error("unexpected detector output shape");
    }

    const int mapHeight = static_cast<int>(shape[2]);
    const int mapWidth = static_cast<int>(shape[3]);
    const float *probability = output.GetTensorData<float>();
#ifdef HAVE_OPENCV
    const cv::Mat probabilityMap(mapHeight, mapWidth, CV_32FC1,
                                 const_cast<float *>(probability));
    cv::Mat bitmap;
    cv::threshold(probabilityMap, bitmap, 0.3, 255.0, cv::THRESH_BINARY);
    bitmap.convertTo(bitmap, CV_8UC1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    std::vector<TextBox> boxes;
    const double scaleX = image.width() / static_cast<double>(mapWidth);
    const double scaleY = image.height() / static_cast<double>(mapHeight);
    for (const auto &contour : contours) {
        // Keep the old connected-component floor as a noise guard. Without
        // it, glyph-like UI icons become independent OCR candidates.
        if (contour.size() < 3 || cv::contourArea(contour) < 100.0) {
            continue;
        }
        const cv::RotatedRect originalRect = cv::minAreaRect(contour);
        if (std::min(originalRect.size.width, originalRect.size.height) < 3.0F)
        {
            continue;
        }

        cv::Mat mask = cv::Mat::zeros(mapHeight, mapWidth, CV_8UC1);
        cv::drawContours(mask, std::vector<std::vector<cv::Point>>{contour}, 0,
                         cv::Scalar(1), cv::FILLED);
        const double score = cv::mean(probabilityMap, mask)[0];
        if (score < 0.6) {
            continue;
        }

        // DB unclip expands the original contour by area / perimeter. Clipper2
        // performs the geometric offset without rasterizing the polygon.
        const double perimeter = cv::arcLength(contour, true);
        const double unclipDistance =
            perimeter > 0.0 ? 1.5 * cv::contourArea(contour) / perimeter : 0.0;
        const auto expanded = unclipContour(contour, unclipDistance);
        if (!expanded) {
            continue;
        }
        const auto mapQuad = orderedQuad(*expanded);

        TextBox box;
        for (size_t i = 0; i < box.quad.size(); ++i) {
            box.quad[i] = cv::Point2f(
                std::clamp(mapQuad[i].x * static_cast<float>(scaleX), 0.0F,
                           static_cast<float>(image.width() - 1)),
                std::clamp(mapQuad[i].y * static_cast<float>(scaleY), 0.0F,
                           static_cast<float>(image.height() - 1)));
        }
        cv::Point2f minPoint = box.quad[0];
        cv::Point2f maxPoint = box.quad[0];
        for (const auto &point : box.quad) {
            minPoint.x = std::min(minPoint.x, point.x);
            minPoint.y = std::min(minPoint.y, point.y);
            maxPoint.x = std::max(maxPoint.x, point.x);
            maxPoint.y = std::max(maxPoint.y, point.y);
        }
        box.left = std::clamp(static_cast<int>(std::floor(minPoint.x)), 0,
                              image.width() - 1);
        box.top = std::clamp(static_cast<int>(std::floor(minPoint.y)), 0,
                             image.height() - 1);
        box.right = std::clamp(static_cast<int>(std::ceil(maxPoint.x)),
                               box.left + 1, image.width());
        box.bottom = std::clamp(static_cast<int>(std::ceil(maxPoint.y)),
                                box.top + 1, image.height());
        box.detectionScore = static_cast<float>(score);
        boxes.push_back(std::move(box));
    }
#else
    std::vector<unsigned char> visited(
        static_cast<size_t>(mapHeight) * mapWidth, 0);
    std::vector<TextBox> boxes;
    constexpr float threshold = 0.3F;
    const double scaleX = image.width() / static_cast<double>(mapWidth);
    const double scaleY = image.height() / static_cast<double>(mapHeight);

    for (int y = 0; y < mapHeight; ++y) {
        for (int x = 0; x < mapWidth; ++x) {
            const size_t start = static_cast<size_t>(y) * mapWidth + x;
            if (visited[start] || probability[start] <= threshold) {
                continue;
            }

            std::queue<std::pair<int, int>> pending;
            pending.push({x, y});
            visited[start] = 1;
            int left = x;
            int right = x;
            int top = y;
            int bottom = y;
            int pixels = 0;
            double score = 0.0;
            while (!pending.empty()) {
                const auto [cx, cy] = pending.front();
                pending.pop();
                ++pixels;
                score += probability[static_cast<size_t>(cy) * mapWidth + cx];
                left = std::min(left, cx);
                right = std::max(right, cx);
                top = std::min(top, cy);
                bottom = std::max(bottom, cy);

                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = cx + dx;
                        const int ny = cy + dy;
                        if (nx < 0 || nx >= mapWidth || ny < 0 ||
                            ny >= mapHeight || (dx == 0 && dy == 0))
                        {
                            continue;
                        }
                        const size_t offset =
                            static_cast<size_t>(ny) * mapWidth + nx;
                        if (!visited[offset] && probability[offset] > threshold)
                        {
                            visited[offset] = 1;
                            pending.push({nx, ny});
                        }
                    }
                }
            }

            const int boxWidth = right - left + 1;
            const int boxHeight = bottom - top + 1;
            if (pixels < 100 || boxWidth < 8 || boxHeight < 3) {
                continue;
            }

            TextBox box;
            box.left = std::max(0, static_cast<int>(left * scaleX) - 8);
            box.top = std::max(0, static_cast<int>(top * scaleY) - 8);
            box.right = std::min(image.width(),
                                 static_cast<int>((right + 1) * scaleX) + 8);
            box.bottom = std::min(image.height(),
                                  static_cast<int>((bottom + 1) * scaleY) + 8);
            box.detectionScore = static_cast<float>(score / pixels);
            if (box.detectionScore < 0.6F) {
                continue;
            }
            boxes.push_back(std::move(box));
        }
    }
#endif

    std::sort(
        boxes.begin(), boxes.end(), [](const TextBox &a, const TextBox &b) {
            const int aCenter = (a.top + a.bottom) / 2;
            const int bCenter = (b.top + b.bottom) / 2;
            const int tolerance =
                std::max(10, std::min(a.bottom - a.top, b.bottom - b.top));
            if (std::abs(aCenter - bCenter) > tolerance) {
                return a.top < b.top;
            }
            return a.left < b.left;
        });
    return boxes;
}

double benchmark(Ort::Session &session, const std::vector<float> &input,
                 const std::vector<int64_t> &shape)
{
    for (int i = 0; i < 5; ++i) {
        const auto output = runModel(session, input, shape);
        (void)output;
    }
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 30; ++i) {
        const auto output = runModel(session, input, shape);
        (void)output;
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    return std::chrono::duration<double, std::milli>(elapsed).count() / 30.0;
}

} // namespace
#endif

int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);

    const std::string imagePath =
        argc >= 2 ? argv[1] : "tests/test_data/paseo_sreenshot.png";
    const std::string detPath = findModel(
        argc >= 3 ? argv[2]
                  : std::string(TALKINPUT_PPOCRV6_MODEL_DIR) + "/det.onnx",
        "third_parties/rapid-ocr/models/ch_PP-OCRv3_det_infer.onnx");
    const std::string recPath = findModel(
        argc >= 4 ? argv[3]
                  : std::string(TALKINPUT_PPOCRV6_MODEL_DIR) + "/rec.onnx",
        "third_parties/rapid-ocr/models/ch_PP-OCRv3_rec_infer.onnx");
    const std::string keysPath =
        argc >= 5
            ? argv[4]
            : std::string(TALKINPUT_PPOCRV6_MODEL_DIR) + "/ppocrv6_keys.txt";

    QFileInfo imageInfo(QString::fromStdString(imagePath));
    if (!imageInfo.exists()) {
        std::fprintf(stderr, "Image not found: %s\n", imagePath.c_str());
        return 1;
    }
    const QImage image(imageInfo.absoluteFilePath());
    if (image.isNull()) {
        std::fprintf(stderr, "QImage load failed: %s\n", imagePath.c_str());
        return 1;
    }

    std::printf("Image: %s (%dx%d) %.1f KB\n",
                imageInfo.absoluteFilePath().toUtf8().constData(),
                image.width(), image.height(), imageInfo.size() / 1024.0);
    std::printf("Det: %s\nRec: %s\nKeys: %s\n\n", detPath.c_str(),
                recPath.c_str(), keysPath.c_str());

#ifndef HAVE_ONNXRUNTIME
    std::printf("ONNX Runtime is not enabled at configure time.\n");
    return 0;
#else
    try {
        Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "ppocrv6");
        Ort::Session detSession(environment, toWstring(detPath).c_str(),
                                sessionOptions());
        Ort::Session recSession(environment, toWstring(recPath).c_str(),
                                sessionOptions());
        const auto dictionary = loadDictionary(keysPath);
        if (dictionary.size() != 18708) {
            std::printf("Warning: dictionary has %zu entries, expected 18708\n",
                        dictionary.size());
        }

        const auto detInput = detectorInput(image, 672, 960);
        const auto recInput = recognizerInput(image);
        std::printf("[ONNX Runtime] det 672x960: %.1f ms/iter\n",
                    benchmark(detSession, detInput, {1, 3, 672, 960}));
        std::printf("[ONNX Runtime] rec 48x320: %.1f ms/iter\n",
                    benchmark(recSession, recInput, {1, 3, 48, 320}));
#ifdef HAVE_OPENCV
        std::printf("[OpenCV] DB contour + score + Clipper2 unclip + "
                    "perspective crop enabled\n");
#endif

        const auto started = std::chrono::steady_clock::now();
        auto boxes = detectText(detSession, image, 672, 960);
#ifdef HAVE_OPENCV
        const cv::Mat rgbImage = qimageToRgbMat(image);
#endif
        for (auto &box : boxes) {
#ifdef HAVE_OPENCV
            const auto crop = perspectiveTextCrop(rgbImage, box.quad);
            const auto input = recognizerInput(crop);
#else
            const QRect rect(box.left, box.top, box.right - box.left,
                             box.bottom - box.top);
            const auto input = recognizerInput(image.copy(rect));
#endif
            const auto output = runModel(recSession, input, {1, 3, 48, 320});
            const auto result = decodeRecognition(output, dictionary);
            box.text = result.text;
            while (!box.text.empty() && box.text.back() == ' ') {
                box.text.pop_back();
            }
            const auto first = box.text.find_first_not_of(' ');
            if (first != std::string::npos && first > 0) {
                box.text.erase(0, first);
            }
            box.recognitionScore = result.score;
        }
        const auto elapsed = std::chrono::steady_clock::now() - started;
        const auto totalMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed)
                .count();

        std::string fullText;
        std::printf("\n=== PP-OCRv6 Result (%zu text lines) ===\n",
                    boxes.size());
        for (const auto &box : boxes) {
            if (box.text.empty()) {
                continue;
            }
            if (!fullText.empty()) {
                fullText += '\n';
            }
            fullText += box.text;
            std::printf("[%4d,%4d - %4d,%4d] det=%.3f rec=%.3f  %s\n", box.left,
                        box.top, box.right, box.bottom, box.detectionScore,
                        box.recognitionScore, box.text.c_str());
        }
        std::printf("\n=== Final Text ===\n%s\n", fullText.c_str());
        std::printf("=== Done ===\n");
        std::printf(
            "End-to-end OCR took %lld ms, %zu text line(s), %zu chars\n",
            static_cast<long long>(totalMs), boxes.size(), fullText.size());
        return fullText.empty() ? 2 : 0;
    }
    catch (const std::exception &error) {
        std::fprintf(stderr, "\nFATAL exception: %s\n", error.what());
        return 2;
    }
    catch (...) {
        std::fprintf(stderr, "\nFATAL unknown exception\n");
        return 2;
    }
#endif
}
