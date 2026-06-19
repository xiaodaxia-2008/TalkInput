#pragma once

#include "json_utils.h"

#include <QString>

namespace talkinput
{

nlohmann::json asrPresets();
nlohmann::json asrPresetById(const QString &id);
QString currentAsrProviderId();
nlohmann::json currentAsrPreset();
void setCurrentAsrProviderId(const QString &id);
bool isSystemAsrPreset(const nlohmann::json &preset);
QString asrModelDir(const nlohmann::json &preset);
bool isAsrPresetInstalled(const nlohmann::json &preset);

} // namespace talkinput
