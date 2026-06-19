#pragma once

#include "json_utils.h"

#include <QString>

namespace talkinput
{

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

} // namespace talkinput
