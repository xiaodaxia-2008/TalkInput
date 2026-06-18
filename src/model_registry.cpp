#include "model_registry.h"
#include "app_config.h"
#include "logging.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace talkinput
{

struct RawModelPreset
{
    std::string name;
    std::string type;
    std::string languages;
    std::string modelDirName;
    std::string url;
    std::int64_t size = 0;
    int paramCount = 0;
    bool streamingSupport = false;
    bool isPunctuationModel = false;
    std::map<std::string, std::string> files;
    nlohmann::json postPunctuationModel;
};

namespace
{

constexpr const char *DefaultLlmEndpoint =
    "http://127.0.0.1:8765/v1/chat/completions";
constexpr const char *DefaultLlmModel = "Qwen3.5-2B-Q4_K_M";

std::vector<ModelPreset> s_asrPresets;
std::vector<ModelPreset> s_toolPresets;
LlmLocalModel s_llmLocalModel;
std::vector<LlmProviderPreset> s_llmProviderPresets;
std::string s_llmSystemPrompt;
std::string s_llmUserPrompt;
bool s_loaded = false;

QString qs(const std::string &value)
{
    return QString::fromStdString(value);
}

ModelPreset parsePreset(const nlohmann::json &obj)
{
    const RawModelPreset raw = obj.get<RawModelPreset>();
    ModelPreset preset;
    preset.name = raw.name;
    preset.type = raw.type;
    preset.languages = raw.languages;
    preset.modelDirName = raw.modelDirName;
    preset.url = raw.url;
    preset.size = raw.size;
    preset.paramCount = raw.paramCount;
    preset.streamingSupport = raw.streamingSupport;
    preset.isPunctuationModel = raw.isPunctuationModel;

    // Parse postPunctuationModel (nested punctuation model config)
    if (raw.postPunctuationModel.is_object()) {
        auto it = raw.postPunctuationModel.find("modelDirName");
        if (it != raw.postPunctuationModel.end() && it->is_string()) {
            preset.postPunctuationModelDirName = it->get<std::string>();
            SPDLOG_DEBUG("model_registry: preset {} has punctuation model {}",
                         preset.name, preset.postPunctuationModelDirName);
        }
    }

    for (const auto &[key, pattern] : raw.files) {
        FileRule rule;
        if (key.ends_with(">dir")) {
            rule.configField = key.substr(0, key.size() - 5);
            rule.isDir = true;
        }
        else {
            rule.configField = key;
            rule.isDir = false;
        }
        rule.globPatterns.push_back(pattern);
        preset.files.push_back(std::move(rule));
    }

    SPDLOG_DEBUG("model_registry: parsing preset {} ({})", preset.name,
                 preset.modelDirName);
    return preset;
}

std::vector<ModelPreset> parsePresetArray(const nlohmann::json &root,
                                          const std::string &key)
{
    std::vector<ModelPreset> presets;
    const nlohmann::json arr = root.value(key, nlohmann::json::array());
    SPDLOG_DEBUG("model_registry: parsing {} with {} items", key, arr.size());
    presets.reserve(arr.size());
    for (const auto &val : arr) {
        presets.push_back(parsePreset(val));
    }
    return presets;
}

LlmLocalModel parseLlmLocalModel(const nlohmann::json &root)
{
    const nlohmann::json llmObj =
        root.value("llmPostProcessing", nlohmann::json::object());
    const nlohmann::json localModelObj =
        llmObj.value("localModel", nlohmann::json::object());
    LlmLocalModel model = localModelObj.get<LlmLocalModel>();
    SPDLOG_DEBUG("model_registry: loaded LLM local model {} ({})", model.name,
                 model.fileName);
    return model;
}

std::vector<LlmProviderPreset>
parseLlmProviderPresets(const nlohmann::json &root)
{
    const nlohmann::json llmObj =
        root.value("llmPostProcessing", nlohmann::json::object());
    const nlohmann::json providerArray =
        llmObj.value("providers", nlohmann::json::array());

    std::vector<LlmProviderPreset> providers;
    providers.reserve(providerArray.size());
    for (const auto &value : providerArray) {
        LlmProviderPreset provider = value.get<LlmProviderPreset>();
        if (provider.models.empty() && !provider.model.empty()) {
            provider.models.push_back(provider.model);
        }

        // ---- Parse per-model pricing ----
        const auto pricingJson =
            value.value("pricing", nlohmann::json::object());
        for (auto it = pricingJson.begin(); it != pricingJson.end(); ++it) {
            provider.modelPricing[it.key()] = it.value().get<LlmPricing>();
        }

        if (!provider.id.empty()) {
            providers.push_back(std::move(provider));
            SPDLOG_DEBUG("model_registry: loaded LLM provider {} ({})",
                         providers.back().id, providers.back().endpoint);
        }
    }

    if (providers.empty()) {
        providers.push_back(LlmProviderPreset{
            .id = "llama.cpp",
            .name = "llama.cpp",
            .endpoint = DefaultLlmEndpoint,
            .model = DefaultLlmModel,
            .models = {DefaultLlmModel},
            .custom = false,
            .managedLocalService = true,
        });
    }
    return providers;
}

std::string parseLlmSystemPrompt(const nlohmann::json &root)
{
    const nlohmann::json llmObj =
        root.value("llmPostProcessing", nlohmann::json::object());
    return llmObj.value("systemPrompt", std::string());
}

std::string parseLlmUserPrompt(const nlohmann::json &root)
{
    const nlohmann::json llmObj =
        root.value("llmPostProcessing", nlohmann::json::object());
    return llmObj.value("userPrompt", std::string());
}

void ensureLoaded()
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;

    const nlohmann::json root = appConfigRoot();
    s_asrPresets = parsePresetArray(root, "asrPresets");
    s_toolPresets = parsePresetArray(root, "toolPresets");
    s_llmLocalModel = parseLlmLocalModel(root);
    s_llmProviderPresets = parseLlmProviderPresets(root);
    s_llmSystemPrompt = parseLlmSystemPrompt(root);
    s_llmUserPrompt = parseLlmUserPrompt(root);

    SPDLOG_INFO("model_registry: loaded {} ASR presets, {} tool presets, {} "
                "LLM providers, LLM model {}",
                s_asrPresets.size(), s_toolPresets.size(),
                s_llmProviderPresets.size(), s_llmLocalModel.fileName);
}

QStringList toQStringList(const std::vector<std::string> &values)
{
    QStringList result;
    result.reserve(static_cast<qsizetype>(values.size()));
    for (const auto &value : values) {
        result.append(qs(value));
    }
    return result;
}

QStringList findFiles(const QDir &dir, const QStringList &names, bool isDir)
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

} // namespace

std::vector<ModelPreset> loadModelPresets()
{
    ensureLoaded();
    return s_asrPresets;
}

std::vector<ModelPreset> loadToolPresets()
{
    ensureLoaded();
    return s_toolPresets;
}

LlmLocalModel loadLlmLocalModel()
{
    ensureLoaded();
    return s_llmLocalModel;
}

std::vector<LlmProviderPreset> loadLlmProviderPresets()
{
    ensureLoaded();
    return s_llmProviderPresets;
}

LlmProviderPreset defaultLlmProvider()
{
    ensureLoaded();
    if (!s_llmProviderPresets.empty()) {
        return s_llmProviderPresets.front();
    }
    return LlmProviderPreset{
        .id = "llama.cpp",
        .name = "llama.cpp",
        .endpoint = DefaultLlmEndpoint,
        .model = DefaultLlmModel,
        .models = {DefaultLlmModel},
        .custom = false,
        .managedLocalService = true,
    };
}

LlmProviderPreset findLlmProviderPreset(const std::string &id)
{
    ensureLoaded();
    for (const auto &provider : s_llmProviderPresets) {
        if (provider.id == id) {
            return provider;
        }
    }
    return defaultLlmProvider();
}

std::string defaultLlmProviderId()
{
    return defaultLlmProvider().id;
}

std::string defaultLlmEndpoint()
{
    const std::string endpoint = defaultLlmProvider().endpoint;
    return endpoint.empty() ? DefaultLlmEndpoint : endpoint;
}

std::string defaultLlmModel()
{
    const std::string model = defaultLlmProvider().model;
    return model.empty() ? DefaultLlmModel : model;
}

std::string defaultLlmSystemPrompt()
{
    ensureLoaded();
    return s_llmSystemPrompt;
}

std::string defaultLlmUserPrompt()
{
    ensureLoaded();
    return s_llmUserPrompt;
}

ModelFileSet resolveModelFiles(const QString &modelDir)
{
    ensureLoaded();

    const QDir dir(modelDir);
    const std::string dirName = dir.dirName().toStdString();

    const ModelPreset *preset = nullptr;
    for (const auto &p : s_asrPresets) {
        if (p.modelDirName == dirName) {
            preset = &p;
            break;
        }
    }

    ModelFileSet result;
    if (!preset) {
        SPDLOG_DEBUG("model_registry: no preset for {}; caller should fall "
                     "back to probing",
                     dirName);
        result.modelDirName = dirName;
        return result;
    }

    result.type = preset->type;
    result.modelDirName = preset->modelDirName;
    result.matched = true;

    for (const auto &rule : preset->files) {
        const QStringList found =
            findFiles(dir, toQStringList(rule.globPatterns), rule.isDir);
        if (found.isEmpty()) {
            SPDLOG_WARN("model_registry: no match for {} in {}",
                        rule.configField, dirName);
            continue;
        }
        result.resolvedFiles.insert(
            {rule.configField, found.first().toStdString()});
    }

    return result;
}

} // namespace talkinput
