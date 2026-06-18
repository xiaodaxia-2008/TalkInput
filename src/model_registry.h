#pragma once

#include <QString>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace talkinput
{

struct FileRule
{
    std::string configField;
    std::vector<std::string> globPatterns;
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

struct ModelFileSet
{
    std::string type;
    std::string modelDirName;
    std::map<std::string, std::string> resolvedFiles;
    bool matched = false;
};

struct LlmLocalModel
{
    std::string name;
    std::string url;
    std::string fileName;
    std::int64_t size = 0;
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
};

std::vector<ModelPreset> loadModelPresets();
std::vector<ModelPreset> loadToolPresets();
LlmLocalModel loadLlmLocalModel();
std::vector<LlmProviderPreset> loadLlmProviderPresets();
LlmProviderPreset defaultLlmProvider();
LlmProviderPreset findLlmProviderPreset(const std::string &id);
std::string defaultLlmProviderId();
std::string defaultLlmEndpoint();
std::string defaultLlmModel();
std::string defaultLlmSystemPrompt();
std::string defaultLlmUserPrompt();
ModelFileSet resolveModelFiles(const QString &modelDir);

} // namespace talkinput
