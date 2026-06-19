#pragma once

#include "json_utils.h"

#include <QString>
#include <string_view>

namespace talkinput
{

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

nlohmann::json findAsrPresetById(const QString &id);
nlohmann::json findLlmPresetById(const QString &id);
QString findAsrPresetIdByName(const QString &name);

} // namespace talkinput
