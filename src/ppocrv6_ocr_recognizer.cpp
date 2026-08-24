#include "ppocrv6_ocr_recognizer.h"

#include "logging.h"

#include <QCoreApplication>
#include <QCoro/QCoroFuture>
#include <QFileInfo>
#include <QPromise>
#include <QThreadPool>

#include <clipper2/clipper.h>
#include <onnxruntime_cxx_api.h>
#include <opencv2/core.hpp>
#include <opencv2/geometry/2d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace talkinput
{

namespace
{

struct TextBox
{
    std::array<cv::Point2f, 4> quad{};
    float detectionScore = 0.0F;
    float recognitionScore = 0.0F;
    std::string text;
};

struct RecognitionInput
{
    std::vector<float> values;
    int width = 0;
};

struct RecognitionResult
{
    std::string text;
    float score = 0.0F;
};

struct AngleResult
{
    bool rotate180 = false;
};

Ort::SessionOptions sessionOptions()
{
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(4);
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    return options;
}

std::basic_string<ORTCHAR_T> modelPath(const QString &path)
{
    if constexpr (std::is_same_v<ORTCHAR_T, wchar_t>) {
        return path.toStdWString();
    }
    else {
        return path.toStdString();
    }
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

struct DetectorInput
{
    std::vector<float> values;
    int contentWidth = 0;
    int contentHeight = 0;
};

DetectorInput detectorInput(const QImage &source, int height, int width)
{
    const QImage rgb = source.convertToFormat(QImage::Format_RGB888);
    const double scale =
        std::min(width / static_cast<double>(std::max(1, rgb.width())),
                 height / static_cast<double>(std::max(1, rgb.height())));
    const int contentWidth =
        std::clamp(static_cast<int>(std::round(rgb.width() * scale)), 1, width);
    const int contentHeight = std::clamp(
        static_cast<int>(std::round(rgb.height() * scale)), 1, height);
    const QImage resized =
        rgb.scaled(contentWidth, contentHeight, Qt::IgnoreAspectRatio,
                   Qt::SmoothTransformation);
    QImage image(width, height, QImage::Format_RGB888);
    image.fill(Qt::black);
    for (int y = 0; y < contentHeight; ++y) {
        std::memcpy(image.scanLine(y), resized.constScanLine(y),
                    static_cast<size_t>(contentWidth) * 3);
    }

    DetectorInput result;
    result.values.resize(static_cast<size_t>(3) * height * width);
    result.contentWidth = contentWidth;
    result.contentHeight = contentHeight;
    for (int y = 0; y < height; ++y) {
        const uchar *line = image.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const size_t offset = static_cast<size_t>(y) * width + x;
            result.values[offset] = line[x * 3 + 2] / 127.5F - 1.0F;
            result.values[static_cast<size_t>(height) * width + offset] =
                line[x * 3 + 1] / 127.5F - 1.0F;
            result.values[static_cast<size_t>(2) * height * width + offset] =
                line[x * 3] / 127.5F - 1.0F;
        }
    }
    return result;
}

cv::Mat qimageToRgbMat(const QImage &source)
{
    const QImage rgb = source.convertToFormat(QImage::Format_RGB888);
    return cv::Mat(rgb.height(), rgb.width(), CV_8UC3,
                   const_cast<uchar *>(rgb.constBits()), rgb.bytesPerLine())
        .clone();
}

RecognitionInput recognizerInput(const cv::Mat &source)
{
    constexpr int height = 48;
    constexpr int maxWidth = 1600;
    if (source.empty() || source.channels() != 3) {
        throw std::runtime_error("invalid PP-OCRv6 recognition crop");
    }

    int resizedWidth =
        std::clamp(static_cast<int>(std::ceil(
                       source.cols * height /
                       static_cast<double>(std::max(1, source.rows)))),
                   1, maxWidth);
    resizedWidth = std::min(maxWidth, (resizedWidth + 3) / 4 * 4);
    cv::Mat image;
    cv::resize(source, image, cv::Size(resizedWidth, height), 0.0, 0.0,
               cv::INTER_LINEAR);

    RecognitionInput result;
    result.values.assign(static_cast<size_t>(3) * height * resizedWidth, 0.0F);
    result.width = resizedWidth;
    for (int y = 0; y < height; ++y) {
        const auto *line = image.ptr<cv::Vec3b>(y);
        for (int x = 0; x < resizedWidth; ++x) {
            const size_t offset = static_cast<size_t>(y) * resizedWidth + x;
            result.values[offset] = line[x][2] / 127.5F - 1.0F;
            result.values[static_cast<size_t>(height) * resizedWidth + offset] =
                line[x][1] / 127.5F - 1.0F;
            result.values[static_cast<size_t>(2) * height * resizedWidth +
                          offset] = line[x][0] / 127.5F - 1.0F;
        }
    }
    return result;
}

std::vector<float> classifierInput(const cv::Mat &source)
{
    constexpr int height = 48;
    constexpr int width = 192;
    const int resizedWidth =
        std::clamp(static_cast<int>(std::ceil(
                       source.cols * height /
                       static_cast<double>(std::max(1, source.rows)))),
                   1, width);
    cv::Mat resized;
    cv::resize(source, resized, cv::Size(resizedWidth, height), 0.0, 0.0,
               cv::INTER_LINEAR);
    cv::Mat image = cv::Mat::zeros(height, width, CV_8UC3);
    resized.copyTo(image(cv::Rect(0, 0, resizedWidth, height)));

    std::vector<float> result(static_cast<size_t>(3) * height * width, 0.0F);
    for (int y = 0; y < height; ++y) {
        const auto *line = image.ptr<cv::Vec3b>(y);
        for (int x = 0; x < width; ++x) {
            const size_t offset = static_cast<size_t>(y) * width + x;
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
    const auto sum = [](const cv::Point2f &point) { return point.x + point.y; };
    const auto difference = [](const cv::Point2f &point) {
        return point.x - point.y;
    };
    const auto bySum = [&](const cv::Point2f &left, const cv::Point2f &right) {
        return sum(left) < sum(right);
    };
    const auto byDifference = [&](const cv::Point2f &left,
                                  const cv::Point2f &right) {
        return difference(left) < difference(right);
    };
    const auto topLeft =
        std::min_element(std::begin(points), std::end(points), bySum);
    const auto bottomRight =
        std::max_element(std::begin(points), std::end(points), bySum);
    const auto bottomLeft =
        std::min_element(std::begin(points), std::end(points), byDifference);
    const auto topRight =
        std::max_element(std::begin(points), std::end(points), byDifference);
    return {*topLeft, *topRight, *bottomRight, *bottomLeft};
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
    return points.size() >= 3 ? std::optional(cv::minAreaRect(points))
                              : std::nullopt;
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
                   8, 1600);
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

std::optional<AngleResult> classifyAngle(Ort::Session &session,
                                         const cv::Mat &crop)
{
    const auto input = classifierInput(crop);
    const auto output = runModel(session, input, {1, 3, 48, 192});
    const auto shape = output.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 2 || shape[0] != 1 || shape[1] < 2) {
        return std::nullopt;
    }
    const float *values = output.GetTensorData<float>();
    const float maxValue = std::max(values[0], values[1]);
    const float exp0 = std::exp(values[0] - maxValue);
    const float exp1 = std::exp(values[1] - maxValue);
    const float confidence = exp1 / (exp0 + exp1);
    return AngleResult{confidence >= 0.9F && values[1] > values[0]};
}

RecognitionResult decodeRecognition(const Ort::Value &output,
                                    const std::vector<std::string> &dictionary)
{
    const auto shape = output.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 3 || shape[0] != 1 || shape[2] <= 1) {
        throw std::runtime_error("unexpected PP-OCRv6 recognition output");
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

std::vector<std::string> loadDictionary(const QString &path)
{
    std::ifstream file(path.toStdString(), std::ios::binary);
    if (!file) {
        throw std::runtime_error("cannot open PP-OCRv6 dictionary");
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

void trimResult(TextBox &box, const RecognitionResult &result)
{
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

std::vector<TextBox> detectText(Ort::Session &session, const QImage &image,
                                int inputHeight, int inputWidth)
{
    const auto input = detectorInput(image, inputHeight, inputWidth);
    const auto output =
        runModel(session, input.values, {1, 3, inputHeight, inputWidth});
    const auto shape = output.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 4 || shape[0] != 1 || shape[1] != 1) {
        throw std::runtime_error("unexpected PP-OCRv6 detector output");
    }

    const int mapHeight = static_cast<int>(shape[2]);
    const int mapWidth = static_cast<int>(shape[3]);
    const float *probability = output.GetTensorData<float>();
    const double scaleX = image.width() /
                          static_cast<double>(input.contentWidth) * inputWidth /
                          static_cast<double>(mapWidth);
    const double scaleY = image.height() /
                          static_cast<double>(input.contentHeight) *
                          inputHeight / static_cast<double>(mapHeight);
    const cv::Mat probabilityMap(mapHeight, mapWidth, CV_32FC1,
                                 const_cast<float *>(probability));
    cv::Mat bitmap;
    cv::threshold(probabilityMap, bitmap, 0.3, 255.0, cv::THRESH_BINARY);
    bitmap.convertTo(bitmap, CV_8UC1);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bitmap, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);
    std::vector<TextBox> boxes;
    for (const auto &contour : contours) {
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
        const double perimeter = cv::arcLength(contour, true);
        const double distance =
            perimeter > 0.0 ? 1.4 * cv::contourArea(contour) / perimeter : 0.0;
        const auto expanded = unclipContour(contour, distance);
        if (!expanded) {
            continue;
        }

        TextBox box;
        const auto mapQuad = orderedQuad(*expanded);
        for (size_t i = 0; i < box.quad.size(); ++i) {
            box.quad[i] = cv::Point2f(
                std::clamp(mapQuad[i].x * static_cast<float>(scaleX), 0.0F,
                           static_cast<float>(image.width() - 1)),
                std::clamp(mapQuad[i].y * static_cast<float>(scaleY), 0.0F,
                           static_cast<float>(image.height() - 1)));
        }
        box.detectionScore = static_cast<float>(score);
        boxes.push_back(std::move(box));
    }

    std::sort(boxes.begin(), boxes.end(),
              [](const TextBox &a, const TextBox &b) {
                  const float aTop =
                      std::min_element(a.quad.begin(), a.quad.end(),
                                       [](const auto &left, const auto &right) {
                                           return left.y < right.y;
                                       })
                          ->y;
                  const float bTop =
                      std::min_element(b.quad.begin(), b.quad.end(),
                                       [](const auto &left, const auto &right) {
                                           return left.y < right.y;
                                       })
                          ->y;
                  if (std::abs(aTop - bTop) > 10.0F) {
                      return aTop < bTop;
                  }
                  return a.quad[0].x < b.quad[0].x;
              });
    return boxes;
}

} // namespace

struct PpOcrV6OcrRecognizer::Impl
{
    Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "talkinput-ppocrv6"};
    std::unique_ptr<Ort::Session> detector;
    std::unique_ptr<Ort::Session> recognizer;
    std::unique_ptr<Ort::Session> classifier;
    std::vector<std::string> dictionary;
};

PpOcrV6OcrRecognizer::PpOcrV6OcrRecognizer(QObject *parent)
    : OcrRecognizer(parent), m_impl(std::make_unique<Impl>())
{
}

PpOcrV6OcrRecognizer::~PpOcrV6OcrRecognizer() = default;

QString PpOcrV6OcrRecognizer::modelDir() const
{
    return QCoreApplication::applicationDirPath() +
           QStringLiteral("/models/ppocrv6_small");
}

bool PpOcrV6OcrRecognizer::isAvailable() const
{
    const QString dir = modelDir();
    const QString keys =
        QFileInfo::exists(dir + QStringLiteral("/ppocrv6_keys2.txt"))
            ? dir + QStringLiteral("/ppocrv6_keys2.txt")
            : dir + QStringLiteral("/ppocrv6_keys.txt");
    return QFileInfo::exists(dir + QStringLiteral("/det.onnx")) &&
           QFileInfo::exists(dir + QStringLiteral("/rec.onnx")) &&
           QFileInfo::exists(keys);
}

bool PpOcrV6OcrRecognizer::ensureInitialized()
{
    if (m_initialized.load(std::memory_order_acquire)) {
        return true;
    }

    std::lock_guard lock(m_initMutex);
    if (m_initialized.load(std::memory_order_relaxed)) {
        return true;
    }
    if (!isAvailable()) {
        SPDLOG_WARN("PP-OCRv6 Small: model files are unavailable in {}",
                    modelDir().toStdString());
        return false;
    }

    try {
        const QString dir = modelDir();
        const QString keys =
            QFileInfo::exists(dir + QStringLiteral("/ppocrv6_keys2.txt"))
                ? dir + QStringLiteral("/ppocrv6_keys2.txt")
                : dir + QStringLiteral("/ppocrv6_keys.txt");
        m_impl->detector = std::make_unique<Ort::Session>(
            m_impl->environment,
            modelPath(dir + QStringLiteral("/det.onnx")).c_str(),
            sessionOptions());
        m_impl->recognizer = std::make_unique<Ort::Session>(
            m_impl->environment,
            modelPath(dir + QStringLiteral("/rec.onnx")).c_str(),
            sessionOptions());
        const QString clsPath = dir + QStringLiteral("/cls.onnx");
        if (QFileInfo::exists(clsPath)) {
            m_impl->classifier = std::make_unique<Ort::Session>(
                m_impl->environment, modelPath(clsPath).c_str(),
                sessionOptions());
        }
        m_impl->dictionary = loadDictionary(keys);
        if (m_impl->dictionary.size() != 18708) {
            SPDLOG_WARN("PP-OCRv6 Small: dictionary has {} entries",
                        m_impl->dictionary.size());
        }
        m_initialized.store(true, std::memory_order_release);
        SPDLOG_INFO("PP-OCRv6 Small: initialized from {}", dir.toStdString());
        return true;
    }
    catch (const std::exception &error) {
        SPDLOG_ERROR("PP-OCRv6 Small: initialization failed: {}", error.what());
        return false;
    }
}

OcrResult PpOcrV6OcrRecognizer::recognizeWithPpOcr(const QImage &image)
{
    if (image.isNull() || !ensureInitialized()) {
        return {};
    }

    constexpr int inputHeight = 672;
    constexpr int inputWidth = 960;
    auto boxes = detectText(*m_impl->detector, image, inputHeight, inputWidth);
    if (boxes.empty()) {
        return {};
    }

    const cv::Mat rgbImage = qimageToRgbMat(image);
    for (auto &box : boxes) {
        const cv::Mat crop = perspectiveTextCrop(rgbImage, box.quad);
        cv::Mat orientedCrop = crop;
        if (m_impl->classifier) {
            const auto angle = classifyAngle(*m_impl->classifier, crop);
            if (angle && angle->rotate180) {
                cv::rotate(crop, orientedCrop, cv::ROTATE_180);
            }
        }
        const auto input = recognizerInput(orientedCrop);
        const auto output = runModel(*m_impl->recognizer, input.values,
                                     {1, 3, 48, input.width});
        trimResult(box, decodeRecognition(output, m_impl->dictionary));
    }

    OcrResult result;
    for (const auto &box : boxes) {
        if (box.text.empty()) {
            continue;
        }
        const QString text = QString::fromUtf8(box.text);
        if (!result.text.isEmpty()) {
            result.text += QLatin1Char('\n');
        }
        result.text += text;

        float left = box.quad[0].x;
        float top = box.quad[0].y;
        float right = left;
        float bottom = top;
        for (const auto &point : box.quad) {
            left = std::min(left, point.x);
            top = std::min(top, point.y);
            right = std::max(right, point.x);
            bottom = std::max(bottom, point.y);
        }
        result.blocks.append(
            {text, QRectF(left, top, right - left, bottom - top)});
    }
    return result;
}

QCoro::Task<OcrResult>
PpOcrV6OcrRecognizer::recognizeDetailed(const QImage &image)
{
    if (image.isNull()) {
        co_return OcrResult{};
    }

    QPromise<OcrResult> promise;
    promise.start();
    auto future = promise.future();
    const QImage imageCopy = image.copy();
    QThreadPool::globalInstance()->start(
        [this, imageCopy, promise = std::move(promise)]() mutable {
            OcrResult result;
            try {
                result = recognizeWithPpOcr(imageCopy);
            }
            catch (const std::exception &error) {
                SPDLOG_WARN("PP-OCRv6 Small: recognition failed: {}",
                            error.what());
            }
            promise.addResult(result);
            promise.finish();
        });

    co_return co_await future;
}

QCoro::Task<QString> PpOcrV6OcrRecognizer::recognizeText(const QImage &image)
{
    const OcrResult result = co_await recognizeDetailed(image);
    co_return result.text;
}

} // namespace talkinput
