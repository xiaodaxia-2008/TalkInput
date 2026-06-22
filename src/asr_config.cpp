#include "asr_config.h"
#include "app_config.h"
#include "utils.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

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

QString asrModelDir(const nlohmann::json &preset)
{
    if (!preset.is_object()) {
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
    const QString modelDir = asrModelDir(preset);
    if (modelDir.isEmpty() || !QFileInfo(modelDir).isDir()) {
        return false;
    }

    const nlohmann::json files =
        preset.value("files", nlohmann::json::object());
    if (files.is_object()) {
        for (auto it = files.begin(); it != files.end(); ++it) {
            if (!it->is_string()) {
                continue;
            }
            const std::string key = it.key();
            const QString relative =
                QString::fromStdString(it->get<std::string>());
            const QFileInfo fi(
                QDir(modelDir).filePath(relative));

            if (key.size() > 4 && key.substr(key.size() - 4) == ">dir") {
                if (!fi.isDir()) {
                    return false;
                }
            } else {
                if (!fi.isFile()) {
                    return false;
                }
            }
        }
    }

    return true;
}

nlohmann::json currentHotwordsConfig()
{
    return appConfigValue("/settings/hotwords");
}

QString hotwordsTextFromConfig(const nlohmann::json &hotwordsConfig)
{
    QStringList lines;
    if (!hotwordsConfig.is_array()) {
        return {};
    }

    for (const auto &item : hotwordsConfig) {
        if (!item.is_string()) {
            continue;
        }

        const QString line =
            QString::fromStdString(item.get<std::string>()).trimmed();
        if (!line.isEmpty()) {
            lines.append(line);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString currentHotwordsText()
{
    return hotwordsTextFromConfig(currentHotwordsConfig());
}

QString hotwordsTextForPreset(const nlohmann::json &preset,
                              const nlohmann::json &hotwordsConfig)
{
    if (!preset.value("hotwordsSupport", false)) {
        return {};
    }
    return hotwordsTextFromConfig(hotwordsConfig);
}

} // namespace talkinput
