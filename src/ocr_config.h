#pragma once

#include "json_utils.h"

#include <QString>

namespace talkinput
{

nlohmann::json ocrPresets();
nlohmann::json ocrPresetById(const QString &id);
QString currentOcrProviderId();
nlohmann::json currentOcrPreset();
void setCurrentOcrProviderId(const QString &id);
bool ocrContextEnabledForAsr();
void setOcrContextEnabledForAsr(bool enabled);

} // namespace talkinput
