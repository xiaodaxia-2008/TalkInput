#include "app_config.h"
#include "logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
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

std::string pointerPath(const QString &path)
{
    QString normalized = path.trimmed();
    if (normalized.isEmpty()) {
        return {};
    }
    if (!normalized.startsWith('/')) {
        normalized.prepend('/');
    }
    normalized.replace("~", "~0");
    return normalized.toStdString();
}

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
        spdlog::warn("config: failed to parse {}: {}", path, e.what());
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
        merged[it.key()] =
            mergeDefaults(merged.value(it.key(), nullptr), it.value());
    }
    return merged;
}

void ensureLoaded()
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;

    s_defaultConfig = readConfigObject(":/resources/config.json");
    const nlohmann::json userConfig = readConfigObject(appConfigPath());
    s_config = userConfig.empty() ? s_defaultConfig
                                  : mergeDefaults(s_defaultConfig, userConfig);

    spdlog::info("config: loaded {}",
                 userConfig.empty() ? "defaults" : appConfigPath());
}

bool writeConfigNow()
{
    const QString path = appConfigPath();
    QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !dir.mkpath(".")) {
        spdlog::warn("config: cannot create directory {}", dir.absolutePath());
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        spdlog::warn("config: cannot write {}", path);
        return false;
    }
    const std::string text = s_config.dump(2);
    file.write(text.data(), static_cast<qint64>(text.size()));
    s_dirty = false;
    spdlog::debug("config: saved {}", path);
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
    QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) {
        dir = QDir::home().filePath(".config/TalkInput");
    }
    return QDir(dir).filePath("config.json");
}

bool appConfigContains(const QString &path)
{
    ensureLoaded();
    const auto pointer = nlohmann::json::json_pointer(pointerPath(path));
    return s_config.contains(pointer);
}

nlohmann::json appConfigValue(const QString &path,
                              const nlohmann::json &fallback)
{
    ensureLoaded();
    const auto pointer = nlohmann::json::json_pointer(pointerPath(path));
    return s_config.contains(pointer) ? s_config.at(pointer) : fallback;
}

QString appConfigString(const QString &path, const QString &fallback)
{
    const nlohmann::json value = appConfigValue(path, fallback);
    return value.is_string() ? value.get<QString>() : fallback;
}

bool appConfigBool(const QString &path, bool fallback)
{
    const nlohmann::json value = appConfigValue(path, fallback);
    return value.is_boolean() ? value.get<bool>() : fallback;
}

void setAppConfigValue(const QString &path, const nlohmann::json &value)
{
    ensureLoaded();
    const auto pointer = nlohmann::json::json_pointer(pointerPath(path));
    s_config[pointer] = value;
    s_dirty = true;
    scheduleSave();
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

} // namespace talkinput
