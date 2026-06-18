#include "model_registry.h"
#include "app_config.h"
#include "logging.h"

#include <QDir>
#include <QFileInfo>

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
static QVector<LlmProviderPreset> s_llmProviderPresets;
static QString s_llmSystemPrompt;
static bool s_loaded = false;

static QString jsonString(const nlohmann::json &obj, const char *key)
{
    if (!obj.is_object() || !obj.contains(key) || !obj.at(key).is_string()) {
        return {};
    }
    return obj.at(key).get<QString>();
}

static ModelPreset parsePreset(const nlohmann::json &obj)
{
    ModelPreset preset;
    preset.name = jsonString(obj, "name");
    preset.typeStr = jsonString(obj, "type");
    preset.languages = jsonString(obj, "languages");
    preset.modelDirName = jsonString(obj, "modelDirName");
    preset.url = jsonString(obj, "url");
    preset.size = obj.value("size", 0);
    preset.paramCount = obj.value("paramCount", 0);
    preset.streamingSupport = obj.value("streamingSupport", false);
    preset.isPunctuationModel = obj.value("isPunctuationModel", false);
    spdlog::debug("model_registry: parsing preset {} ({})", preset.name,
                  preset.modelDirName);

    const nlohmann::json filesObj =
        obj.value("files", nlohmann::json::object());
    for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
        const QString key = QString::fromStdString(it.key());

        ModelPreset::FileRule rule;
        if (key.endsWith(QStringLiteral(">dir"))) {
            rule.configField = key.chopped(5);
            rule.isDir = true;
        }
        else {
            rule.configField = key;
            rule.isDir = false;
        }
        if (it.value().is_string()) {
            rule.globPatterns.append(it.value().get<QString>());
        }
        preset.files.append(rule);
    }

    return preset;
}

static QVector<ModelPreset> parsePresetArray(const nlohmann::json &root,
                                             const QString &key)
{
    QVector<ModelPreset> presets;
    const nlohmann::json arr =
        root.value(key.toStdString(), nlohmann::json::array());
    spdlog::debug("model_registry: parsing {} with {} items", key, arr.size());
    for (const auto &val : arr) {
        presets.append(parsePreset(val));
    }
    return presets;
}

static LlmLocalModel parseLlmLocalModel(const nlohmann::json &root)
{
    const nlohmann::json llmObj =
        root.value("llmPostProcessing", nlohmann::json::object());
    const nlohmann::json localModelObj =
        llmObj.value("localModel", nlohmann::json::object());

    LlmLocalModel model;
    model.name = jsonString(localModelObj, "name");
    model.url = jsonString(localModelObj, "url");
    model.fileName = jsonString(localModelObj, "fileName");
    model.size = localModelObj.value("size", 0);
    spdlog::debug("model_registry: loaded LLM local model {} ({})", model.name,
                  model.fileName);
    return model;
}

static QVector<LlmProviderPreset>
parseLlmProviderPresets(const nlohmann::json &root)
{
    const nlohmann::json llmObj =
        root.value("llmPostProcessing", nlohmann::json::object());
    const nlohmann::json providerArray =
        llmObj.value("providers", nlohmann::json::array());

    QVector<LlmProviderPreset> providers;
    providers.reserve(providerArray.size());
    for (const auto &value : providerArray) {
        const nlohmann::json obj =
            value.is_object() ? value : nlohmann::json::object();
        LlmProviderPreset provider;
        provider.id = jsonString(obj, "id");
        provider.name = jsonString(obj, "name");
        provider.endpoint = jsonString(obj, "endpoint");
        provider.model = jsonString(obj, "model");
        const nlohmann::json modelArray =
            obj.value("models", nlohmann::json::array());
        for (const auto &modelValue : modelArray) {
            const QString model =
                (modelValue.is_string() ? modelValue.get<QString>() : QString())
                    .trimmed();
            if (!model.isEmpty()) {
                provider.models.append(model);
            }
        }
        if (provider.models.isEmpty() && !provider.model.isEmpty()) {
            provider.models.append(provider.model);
        }
        provider.custom = obj.value("custom", false);
        provider.managedLocalService = obj.value("managedLocalService", false);
        if (!provider.id.isEmpty()) {
            providers.append(provider);
            spdlog::debug("model_registry: loaded LLM provider {} ({})",
                          provider.id, provider.endpoint);
        }
    }

    if (providers.isEmpty()) {
        providers.append({"llama.cpp",
                          "llama.cpp",
                          DefaultLlmEndpoint,
                          DefaultLlmModel,
                          {DefaultLlmModel},
                          false,
                          true});
    }
    return providers;
}

static QString parseLlmSystemPrompt(const nlohmann::json &root)
{
    const nlohmann::json llmObj =
        root.value("llmPostProcessing", nlohmann::json::object());
    return jsonString(llmObj, "systemPrompt");
}

static void ensureLoaded()
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;

    const nlohmann::json root = appConfigRoot();
    s_asrPresets = parsePresetArray(root, QStringLiteral("asrPresets"));
    s_toolPresets = parsePresetArray(root, QStringLiteral("toolPresets"));
    s_llmLocalModel = parseLlmLocalModel(root);
    s_llmProviderPresets = parseLlmProviderPresets(root);
    s_llmSystemPrompt = parseLlmSystemPrompt(root);

    spdlog::info("model_registry: loaded {} ASR presets, {} tool presets, {} "
                 "LLM providers, LLM model {}",
                 s_asrPresets.size(), s_toolPresets.size(),
                 s_llmProviderPresets.size(), s_llmLocalModel.fileName);
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

QVector<LlmProviderPreset> loadLlmProviderPresets()
{
    ensureLoaded();
    return s_llmProviderPresets;
}

LlmProviderPreset defaultLlmProvider()
{
    ensureLoaded();
    if (!s_llmProviderPresets.isEmpty()) {
        return s_llmProviderPresets.first();
    }
    return {"llama.cpp",
            "llama.cpp",
            DefaultLlmEndpoint,
            DefaultLlmModel,
            {DefaultLlmModel},
            false,
            true};
}

LlmProviderPreset findLlmProviderPreset(const QString &id)
{
    ensureLoaded();
    for (const auto &provider : s_llmProviderPresets) {
        if (provider.id == id) {
            return provider;
        }
    }
    return defaultLlmProvider();
}

QString defaultLlmProviderId()
{
    return defaultLlmProvider().id;
}

QString defaultLlmEndpoint()
{
    const QString endpoint = defaultLlmProvider().endpoint.trimmed();
    return endpoint.isEmpty() ? QString::fromUtf8(DefaultLlmEndpoint)
                              : endpoint;
}

QString defaultLlmModel()
{
    const QString model = defaultLlmProvider().model.trimmed();
    return model.isEmpty() ? QString::fromUtf8(DefaultLlmModel) : model;
}

QString defaultLlmSystemPrompt()
{
    ensureLoaded();
    return s_llmSystemPrompt;
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
