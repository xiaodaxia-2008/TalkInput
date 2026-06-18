#pragma once

#include <QString>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace talkinput
{

struct FileRule
{
    std::string configField;
    std::string relativePath;
    bool isDir = false;
};

struct ModelPreset
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
    std::string postPunctuationModelDirName;
    std::vector<FileRule> files;
};

struct LlmLocalModel
{
    std::string name;
    std::string url;
    std::string fileName;
    std::int64_t size = 0;
};

struct LlmPricing
{
    double inputPer1M = 0;
    double outputPer1M = 0;
    double cacheHitInputPer1M = 0;
    double cacheMissInputPer1M = 0;
};

struct LlmProviderPreset
{
    std::string id;
    std::string name;
    std::string endpoint;
    std::string model;
    std::vector<std::string> models;
    bool custom = false;
    bool managedLocalService = false;
    std::map<std::string, LlmPricing> modelPricing;
};

std::vector<ModelPreset> loadModelPresets();
std::vector<ModelPreset> loadToolPresets();
std::optional<ModelPreset> findModelPresetByDirName(const std::string &dirName);
std::optional<ModelPreset> findModelPresetByDirectory(const QString &modelDir);
std::optional<ModelPreset> findToolPresetByDirName(const std::string &dirName);
LlmLocalModel loadLlmLocalModel();
std::vector<LlmProviderPreset> loadLlmProviderPresets();
LlmProviderPreset defaultLlmProvider();
LlmProviderPreset findLlmProviderPreset(const std::string &id);
std::string defaultLlmProviderId();
std::string defaultLlmEndpoint();
std::string defaultLlmModel();
std::string defaultLlmSystemPrompt();
std::string defaultLlmUserPrompt();

} // namespace talkinput
