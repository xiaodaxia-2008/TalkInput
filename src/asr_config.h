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
QString asrModelDir(const nlohmann::json &preset);
bool isAsrPresetInstalled(const nlohmann::json &preset);
nlohmann::json currentHotwordsConfig();
QString hotwordsTextFromConfig(const nlohmann::json &hotwordsConfig);
QString currentHotwordsText();
QString hotwordsTextForPreset(const nlohmann::json &preset,
                              const nlohmann::json &hotwordsConfig);

} // namespace talkinput
