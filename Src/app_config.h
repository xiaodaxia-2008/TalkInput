#pragma once

#include "json_utils.h"

#include <QString>

namespace talkinput
{

nlohmann::json appConfigRoot();
QString appConfigPath();
bool appConfigContains(const QString &path);
nlohmann::json appConfigValue(const QString &path,
                              const nlohmann::json &fallback = {});
QString appConfigString(const QString &path, const QString &fallback = {});
bool appConfigBool(const QString &path, bool fallback = false);
void setAppConfigValue(const QString &path, const nlohmann::json &value);
bool saveAppConfig();

} // namespace talkinput
