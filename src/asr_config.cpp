#include "asr_config.h"
#include "app_config.h"
#include "utils.h"

#include <QDir>
#include <QFileInfo>

namespace talkinput
{

nlohmann::json asrPresets()
{
    return appConfigValue("/asrPresets");
}

nlohmann::json asrPresetById(const QString &id)
{
    if (id.isEmpty()) {
        return nlohmann::json::object();
    }
    return appConfigValue(("/asrPresets/" + id).toStdString());
}

QString currentAsrProviderId()
{
    return appConfigString("/settings/asr/providerId");
}

nlohmann::json currentAsrPreset()
{
    return asrPresetById(currentAsrProviderId());
}

void setCurrentAsrProviderId(const QString &id)
{
    setAppConfigValue("/settings/asr/providerId", id.toStdString());
}

bool isSystemAsrPreset(const nlohmann::json &preset)
{
    return jsonString(preset, "type") == QLatin1StringView("System");
}

QString asrModelDir(const nlohmann::json &preset)
{
    if (!preset.is_object() || isSystemAsrPreset(preset)) {
        return {};
    }

    const QString dirName = jsonString(preset, "modelDirName");
    if (dirName.isEmpty()) {
        return {};
    }
    return QDir(appDataDir())
        .filePath(QStringLiteral("models/%1").arg(dirName));
}

bool isAsrPresetInstalled(const nlohmann::json &preset)
{
    if (!preset.is_object()) {
        return false;
    }
    if (isSystemAsrPreset(preset)) {
        return true;
    }
    const QString modelDir = asrModelDir(preset);
    return !modelDir.isEmpty() && QFileInfo(modelDir).isDir();
}

} // namespace talkinput
