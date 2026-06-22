#pragma once

#include "json_utils.h"

#include <QString>
#include <string_view>

class QObject;
class QTranslator;

namespace talkinput
{

// ── Config loading / saving ──────────────────────────────────────

nlohmann::json appConfigRoot();
QString appConfigPath();
bool appConfigContains(std::string_view path);
nlohmann::json appConfigValue(std::string_view path,
                              const nlohmann::json &fallback = {});
QString appConfigString(std::string_view path, std::string_view fallback = {});
bool appConfigBool(std::string_view path, bool fallback = false);
void setAppConfigValue(std::string_view path, const nlohmann::json &value);
bool resetAppConfigToDefaults();
bool saveAppConfig();

// ── Language ─────────────────────────────────────────────────────

QString systemAppLanguage();
QString currentAppLanguage();
void installAppTranslations(const QString &language, QObject *parent,
                            QTranslator *&appTranslator,
                            QTranslator *&qtTranslator);

// ── ASR presets / config ─────────────────────────────────────────

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

// ── LLM presets / config ─────────────────────────────────────────

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

// ── OCR presets / config ─────────────────────────────────────────

nlohmann::json ocrPresets();
nlohmann::json ocrPresetById(const QString &id);
QString currentOcrProviderId();
nlohmann::json currentOcrPreset();
void setCurrentOcrProviderId(const QString &id);
bool ocrContextEnabledForAsr();
void setOcrContextEnabledForAsr(bool enabled);

} // namespace talkinput
