#pragma once

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

    // Resolved at runtime
    std::string resolvedModelDir;
    std::map<std::string, std::string> resolvedFiles;
    std::string hotwordsText;
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
    std::string language = "system";
    bool startMinimized = false;
    std::vector<std::string> hotwords;
    std::string asrProviderId;
    std::string ocrProviderId;
    std::string llmProviderId;
    std::string llmSystemPrompt;
    std::string llmUserPrompt;
    std::string activeMode = "asr_llm_ocr";
    std::string triggerHotkey = "Ctrl+Alt+Space";
    std::string modeSwitchHotkey = "Ctrl+Alt+Enter";
    bool useClipboard = false;
    bool copyToClipboard = false;
    bool restoreClipboard = true;
    bool saveOcrScreenshot = false;
    bool saveAsrAudio = false;
    int pasteDelayMs = 200;
    double overlayOpacity = 0.8;
};

struct AppConfigData
{
    AppSettings settings;
    std::map<std::string, AsrPreset> asrPresets;
    std::map<std::string, LlmPreset> llmPresets;
    std::map<std::string, OcrPreset> ocrPresets;
};

// ═══════════════════════════════════════════════════════════════════
// Global config access
// ═══════════════════════════════════════════════════════════════════

AppConfigData &appConfig();
void markConfigDirty();
bool resetAppConfigToDefaults();
bool saveAppConfig();

// ═══════════════════════════════════════════════════════════════════
// Utility
// ═══════════════════════════════════════════════════════════════════

QString appConfigPath();

// ═══════════════════════════════════════════════════════════════════
// Language
// ═══════════════════════════════════════════════════════════════════

QString systemAppLanguage();
QString currentAppLanguage();
void installAppTranslations(const QString &language, QObject *parent,
                            QTranslator *&appTranslator,
                            QTranslator *&qtTranslator);

} // namespace talkinput
