#include "ocr_config.h"
#include "app_config.h"

namespace talkinput
{

nlohmann::json ocrPresets()
{
    return appConfigValue("/ocrPresets");
}

nlohmann::json ocrPresetById(const QString &id)
{
    if (id.isEmpty()) {
        return nlohmann::json::object();
    }
    return appConfigValue(("/ocrPresets/" + id).toStdString());
}

QString currentOcrProviderId()
{
    return appConfigString("/settings/ocr/providerId");
}

nlohmann::json currentOcrPreset()
{
    nlohmann::json preset = ocrPresetById(currentOcrProviderId());
    if (preset.is_object() && !preset.empty()) {
        return preset;
    }
    return {{"type", "System"}};
}

void setCurrentOcrProviderId(const QString &id)
{
    setAppConfigValue("/settings/ocr/providerId", id.toStdString());
}

bool ocrContextEnabledForAsr()
{
    return appConfigBool("/settings/ocr/ocrContextEnableForAsr", false);
}

void setOcrContextEnabledForAsr(bool enabled)
{
    setAppConfigValue("/settings/ocr/ocrContextEnableForAsr", enabled);
}

} // namespace talkinput
