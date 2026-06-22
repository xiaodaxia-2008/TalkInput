#pragma once

#include "json_utils.h"

#include <QString>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

class QObject;
class QTranslator;

namespace talkinput
{

// ═══════════════════════════════════════════════════════════════════
// Config structs — typed mirror of resources/misc/config.json
// ═══════════════════════════════════════════════════════════════════

struct AsrPresetParams
{
    int sampleRate = 16000;
    int featureDim = 80;
    int numThreads = 2;
    std::string modelingUnit = "cjkchar";
    double hotwordsScore = 1.5;
    std::string language = "zh";
    bool senseVoiceUseItn = true;
    std::string funasrSystemPrompt;
    std::string funasrUserPrompt;
    int funasrMaxNewTokens = 128;
    double funasrTemperature = 1e-6;
    double funasrTopP = 0.8;
    int funasrSeed = 42;
    bool funasrItn = true;
};

struct AsrPreset
{
    std::string id;
    std::string name;
    std::string type;
    std::string languages;
    std::string modelDirName;
    std::string url;
    int64_t size = 0;
    int paramCount = 0;
    bool streamingSupport = false;
    bool hotwordsSupport = false;
    AsrPresetParams params;
    std::map<std::string, std::string> files;
};

struct LlmModelPrice
{
    double inputPer1M = 0;
    double outputPer1M = 0;
    double cacheHitInputPer1M = 0;
    double cacheMissInputPer1M = 0;
};

struct LlmModel
{
    std::string name;
    std::string url;
    std::string fileName;
    int64_t size = 0;
    LlmModelPrice price;
};

struct LlmPreset
{
    std::string id;
    std::string name;
    std::string endpoint;
    std::string apiKey;
    std::string currentModel;
    std::map<std::string, LlmModel> models;
    bool managedLocalService = false;
    int localServicePort = 0;
    int localServiceMaxHealthAttempts = 0;
    std::map<std::string, std::string> localServiceArchiveUrl;
};

struct OcrPreset
{
    std::string id;
    std::string name;
    std::string type;
};

struct AppSettings
{
    struct App
    {
        std::string language = "system";
        bool startMinimized = false;
    } app;

    std::vector<std::string> hotwords;

    struct Asr
    {
        std::string providerId;
    } asr;

    struct Ocr
    {
        std::string providerId;
    } ocr;

    struct Llm
    {
        std::string providerId;
        std::string systemPrompt;
        std::string userPrompt;
    } llm;

    struct Hotkeys
    {
        std::string asr;
        std::string asrLlm;
        std::string asrLlmOcr;
    } hotkeys;
};

struct AppConfigData
{
    AppSettings settings;
    std::map<std::string, AsrPreset> asrPresets;
    std::map<std::string, LlmPreset> llmPresets;
    std::map<std::string, OcrPreset> ocrPresets;
};

// ═══════════════════════════════════════════════════════════════════
// Serialization / deserialization
// ═══════════════════════════════════════════════════════════════════

void from_json(const nlohmann::json &j, AppConfigData &c);
void to_json(nlohmann::json &j, const AppConfigData &c);

// ═══════════════════════════════════════════════════════════════════
// Global config access
// ═══════════════════════════════════════════════════════════════════

const AppConfigData &appConfig();
void setAppConfig(const AppConfigData &config);
bool saveAppConfig();

// ═══════════════════════════════════════════════════════════════════
// Config helpers
// ═══════════════════════════════════════════════════════════════════

QString appConfigPath();

// ── Language ─────────────────────────────────────────────────────

QString systemAppLanguage();
QString currentAppLanguage();
void installAppTranslations(const QString &language, QObject *parent,
                            QTranslator *&appTranslator,
                            QTranslator *&qtTranslator);

// ── Legacy JSON accessors (kept for gradual migration) ──────────

nlohmann::json appConfigRoot();
bool appConfigContains(std::string_view path);
nlohmann::json appConfigValue(std::string_view path,
                              const nlohmann::json &fallback = {});
QString appConfigString(std::string_view path, std::string_view fallback = {});
bool appConfigBool(std::string_view path, bool fallback = false);
void setAppConfigValue(std::string_view path, const nlohmann::json &value);
bool resetAppConfigToDefaults();

nlohmann::json asrPresets();
nlohmann::json asrPresetById(const QString &id);
QString currentAsrProviderId();
nlohmann::json currentAsrPreset();
void setCurrentAsrProviderId(const QString &id);
QString asrModelDir(const nlohmann::json &preset);
bool isAsrPresetInstalled(const nlohmann::json &preset);
nlohmann::json currentHotwordsConfig();
QString hotwordsTextFromConfig(const nlohmann::json &hotwordsConfig);
QString currentHotwordsText();
QString hotwordsTextForPreset(const nlohmann::json &preset,
                              const nlohmann::json &hotwordsConfig);

nlohmann::json llmPresets();
nlohmann::json firstLlmProviderPreset();
nlohmann::json llmProviderPreset(const QString &id);
QString currentLlmProviderId();
nlohmann::json currentLlmProviderPreset();
void setCurrentLlmProviderId(const QString &id);
void setLlmProviderSetting(const QString &id, const QString &key,
                           const nlohmann::json &value);
QString llmProviderEndpoint(const nlohmann::json &provider);
QString llmProviderModel(const nlohmann::json &provider);
QString llmProviderApiKey(const nlohmann::json &provider);
bool llmProviderUsesManagedLocalService(const nlohmann::json &provider);

nlohmann::json ocrPresets();
nlohmann::json ocrPresetById(const QString &id);
QString currentOcrProviderId();
nlohmann::json currentOcrPreset();
void setCurrentOcrProviderId(const QString &id);
bool ocrContextEnabledForAsr();
void setOcrContextEnabledForAsr(bool enabled);

} // namespace talkinput
