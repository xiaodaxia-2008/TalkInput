#include "model_registry.h"
#include "logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace talkinput
{

namespace
{

constexpr const char *DefaultLlmEndpoint =
    "http://127.0.0.1:8765/v1/chat/completions";
constexpr const char *DefaultLlmModel = "Qwen3.5-2B-Q4_K_M";

} // namespace

static QVector<ModelPreset> s_asrPresets;
static QVector<ModelPreset> s_toolPresets;
static LlmLocalModel s_llmLocalModel;
static bool s_loaded = false;

static ModelPreset parsePreset(const QJsonObject &obj)
{
    ModelPreset preset;
    preset.name = obj.value(QStringLiteral("name")).toString();
    preset.typeStr = obj.value(QStringLiteral("type")).toString();
    preset.languages = obj.value(QStringLiteral("languages")).toString();
    preset.modelDirName = obj.value(QStringLiteral("modelDirName")).toString();
    preset.url = obj.value(QStringLiteral("url")).toString();
    preset.size =
        static_cast<qint64>(obj.value(QStringLiteral("size")).toDouble());
    preset.paramCount = obj.value(QStringLiteral("paramCount")).toInt();
    preset.streamingSupport =
        obj.value(QStringLiteral("streamingSupport")).toBool();
    preset.isPunctuationModel =
        obj.value(QStringLiteral("isPunctuationModel")).toBool();
    spdlog::debug("model_registry: parsing preset {} ({})", preset.name,
                  preset.modelDirName);

    const QJsonObject filesObj = obj.value(QStringLiteral("files")).toObject();
    for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
        const QString key = it.key();

        ModelPreset::FileRule rule;
        if (key.endsWith(QStringLiteral(">dir"))) {
            rule.configField = key.chopped(5);
            rule.isDir = true;
        }
        else {
            rule.configField = key;
            rule.isDir = false;
        }
        rule.globPatterns.append(it.value().toString());
        preset.files.append(rule);
    }

    return preset;
}

static QVector<ModelPreset> parsePresetArray(const QJsonObject &root,
                                             const QString &key)
{
    QVector<ModelPreset> presets;
    const QJsonArray arr = root.value(key).toArray();
    spdlog::debug("model_registry: parsing {} with {} items", key, arr.size());
    for (const auto &val : arr) {
        presets.append(parsePreset(val.toObject()));
    }
    return presets;
}

static LlmLocalModel parseLlmLocalModel(const QJsonObject &root)
{
    const QJsonObject llmObj =
        root.value(QStringLiteral("llmPostProcessing")).toObject();
    const QJsonObject localModelObj =
        llmObj.value(QStringLiteral("localModel")).toObject();

    LlmLocalModel model;
    model.name = localModelObj.value(QStringLiteral("name")).toString();
    model.url = localModelObj.value(QStringLiteral("url")).toString();
    model.fileName = localModelObj.value(QStringLiteral("fileName")).toString();
    model.size = static_cast<qint64>(
        localModelObj.value(QStringLiteral("size")).toDouble());
    spdlog::debug("model_registry: loaded LLM local model {} ({})", model.name,
                  model.fileName);
    return model;
}

static void ensureLoaded()
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;

    QFile f(QStringLiteral(":/resources/models.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        spdlog::warn("model_registry: cannot open models.json resource");
        return;
    }
    const QByteArray data = f.readAll();
    f.close();
    spdlog::debug("model_registry: read models.json, {} bytes", data.size());

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        spdlog::warn("model_registry: JSON parse error: {}", err.errorString());
        return;
    }

    const QJsonObject root = doc.object();
    s_asrPresets = parsePresetArray(root, QStringLiteral("asrPresets"));
    s_toolPresets = parsePresetArray(root, QStringLiteral("toolPresets"));
    s_llmLocalModel = parseLlmLocalModel(root);

    spdlog::info("model_registry: loaded {} ASR presets, {} tool presets, LLM "
                 "model {}",
                 s_asrPresets.size(), s_toolPresets.size(),
                 s_llmLocalModel.fileName);
}

QVector<ModelPreset> loadModelPresets()
{
    ensureLoaded();
    return s_asrPresets;
}

QVector<ModelPreset> loadToolPresets()
{
    ensureLoaded();
    return s_toolPresets;
}

LlmLocalModel loadLlmLocalModel()
{
    ensureLoaded();
    return s_llmLocalModel;
}

QString defaultLlmEndpoint()
{
    return QString::fromUtf8(DefaultLlmEndpoint);
}

QString defaultLlmModel()
{
    return QString::fromUtf8(DefaultLlmModel);
}

static QStringList findFiles(const QDir &dir, const QStringList &names,
                             bool isDir)
{
    for (const QString &name : names) {
        const QString full = dir.absoluteFilePath(name);
        if (isDir) {
            if (QFileInfo(full).isDir()) {
                return {full};
            }
        }
        else {
            if (QFileInfo::exists(full) && QFileInfo(full).isFile()) {
                return {full};
            }
        }
    }
    return {};
}

ModelFileSet resolveModelFiles(const QString &modelDir)
{
    ensureLoaded();

    const QDir dir(modelDir);
    const QString dirName = dir.dirName();

    // Find matching preset
    const ModelPreset *preset = nullptr;
    for (const auto &p : s_asrPresets) {
        if (p.modelDirName == dirName) {
            preset = &p;
            break;
        }
    }

    ModelFileSet result;
    if (!preset) {
        spdlog::debug("model_registry: no preset for {}; caller should fall "
                      "back to probing",
                      dirName);
        result.modelDirName = dirName;
        return result;
    }

    result.typeStr = preset->typeStr;
    result.modelDirName = preset->modelDirName;
    result.matched = true;

    for (const auto &rule : preset->files) {
        const QStringList found = findFiles(dir, rule.globPatterns, rule.isDir);
        if (found.isEmpty()) {
            spdlog::warn("model_registry: no match for {} in {}",
                         rule.configField, dirName);
            continue;
        }
        // Use the first match (preferring int8)
        result.resolvedFiles.insert(rule.configField, found.first());
    }

    return result;
}

} // namespace talkinput
