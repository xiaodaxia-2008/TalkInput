#include "app_config.h"
#include "logging.h"
#include "utils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace talkinput
{
namespace
{

nlohmann::json s_defaultConfig;
nlohmann::json s_config;
bool s_loaded = false;
bool s_dirty = false;
QTimer *s_saveTimer = nullptr;

nlohmann::json readConfigObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return nlohmann::json::object();
    }

    try {
        const QByteArray data = file.readAll();
        nlohmann::json parsed = nlohmann::json::parse(
            data.constData(), data.constData() + data.size());
        return parsed.is_object() ? parsed : nlohmann::json::object();
    }
    catch (const nlohmann::json::exception &e) {
        SPDLOG_WARN("config: failed to parse {}: {}", path, e.what());
        return nlohmann::json::object();
    }
}

nlohmann::json mergeDefaults(const nlohmann::json &defaults,
                             const nlohmann::json &user)
{
    if (!defaults.is_object() || !user.is_object()) {
        return user.is_discarded() || user.is_null() ? defaults : user;
    }

    nlohmann::json merged = defaults;
    for (auto it = user.begin(); it != user.end(); ++it) {
        const nlohmann::json defaultValue = merged.contains(it.key())
                                                ? merged.at(it.key())
                                                : nlohmann::json(nullptr);
        merged[it.key()] = mergeDefaults(defaultValue, it.value());
    }
    return merged;
}

void ensureLoaded()
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;

    try {
        s_defaultConfig = readConfigObject(":/resources/misc/config.json");
        const nlohmann::json userConfig = readConfigObject(appConfigPath());
        s_config = userConfig.empty()
                       ? s_defaultConfig
                       : mergeDefaults(s_defaultConfig, userConfig);

        SPDLOG_INFO("config: loaded {}",
                    userConfig.empty() ? "defaults" : appConfigPath());
    }
    catch (const nlohmann::json::exception &e) {
        SPDLOG_WARN("config: failed to load merged config: {}", e.what());
        s_config = s_defaultConfig.empty() ? nlohmann::json::object()
                                           : s_defaultConfig;
    }
}

bool writeConfigNow()
{
    const QString path = appConfigPath();
    QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !dir.mkpath(".")) {
        SPDLOG_WARN("config: cannot create directory {}", dir.absolutePath());
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        SPDLOG_WARN("config: cannot write {}", path);
        return false;
    }
    const std::string text = s_config.dump(2);
    file.write(text.data(), static_cast<qint64>(text.size()));
    s_dirty = false;
    SPDLOG_DEBUG("config: saved {}", path);
    return true;
}

void scheduleSave()
{
    if (!QCoreApplication::instance()) {
        writeConfigNow();
        return;
    }

    if (!s_saveTimer) {
        s_saveTimer = new QTimer(QCoreApplication::instance());
        s_saveTimer->setSingleShot(true);
        s_saveTimer->setInterval(500);
        QObject::connect(s_saveTimer, &QTimer::timeout, []() {
            if (s_dirty) {
                writeConfigNow();
            }
        });
    }
    s_saveTimer->start();
}

} // namespace

nlohmann::json appConfigRoot()
{
    ensureLoaded();
    return s_config;
}

QString appConfigPath()
{
    return QDir(talkinput::appDataDir()).filePath("config.json");
}

bool appConfigContains(std::string_view path)
{
    ensureLoaded();
    const auto pointer = nlohmann::json::json_pointer(std::string{path});
    return s_config.contains(pointer);
}

nlohmann::json appConfigValue(std::string_view path,
                              const nlohmann::json &fallback)
{
    ensureLoaded();
    const auto pointer = nlohmann::json::json_pointer(std::string{path});
    return s_config.contains(pointer) ? s_config.at(pointer) : fallback;
}

QString appConfigString(std::string_view path, std::string_view fallback)
{
    const nlohmann::json value = appConfigValue(path, std::string{fallback});
    if (value.is_string()) {
        return value.get<QString>();
    }
    return QString::fromUtf8(fallback.data(),
                             static_cast<qsizetype>(fallback.size()));
}

bool appConfigBool(std::string_view path, bool fallback)
{
    const nlohmann::json value = appConfigValue(path, fallback);
    return value.is_boolean() ? value.get<bool>() : fallback;
}

void setAppConfigValue(std::string_view path, const nlohmann::json &value)
{
    ensureLoaded();
    const auto pointer = nlohmann::json::json_pointer(std::string{path});
    s_config[pointer] = value;
    s_dirty = true;
    scheduleSave();
}

bool resetAppConfigToDefaults()
{
    ensureLoaded();
    s_config =
        s_defaultConfig.empty() ? nlohmann::json::object() : s_defaultConfig;
    s_dirty = true;
    if (s_saveTimer) {
        s_saveTimer->stop();
    }
    SPDLOG_INFO("config: resetting user config to defaults");
    return writeConfigNow();
}

bool saveAppConfig()
{
    ensureLoaded();
    if (s_saveTimer) {
        s_saveTimer->stop();
    }
    if (!s_dirty && QFileInfo::exists(appConfigPath())) {
        return true;
    }
    return writeConfigNow();
}

nlohmann::json findAsrPresetByName(const QString &name)
{
    const nlohmann::json presets = appConfigValue("/asrPresets");
    if (!presets.is_array()) {
        return nlohmann::json::object();
    }
    const std::string nameStr = name.toStdString();
    for (const auto &preset : presets) {
        if (preset.is_object() && preset.value("name", std::string()) == nameStr) {
            return preset;
        }
    }
    return nlohmann::json::object();
}

} // namespace talkinput
