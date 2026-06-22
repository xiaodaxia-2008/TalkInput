#include "app_config.h"
#include "logging.h"
#include "utils.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QStringList>
#include <QTimer>
#include <QTranslator>

namespace talkinput
{

// ═══════════════════════════════════════════════════════════════════
// Internal state
// ═══════════════════════════════════════════════════════════════════

namespace
{

AppConfigData s_defaultConfig;
AppConfigData s_config;
bool s_loaded = false;
bool s_dirty = false;
QTimer *s_saveTimer = nullptr;

void scheduleSave()
{
    if (!QCoreApplication::instance()) {
        saveAppConfig();
        return;
    }

    if (!s_saveTimer) {
        s_saveTimer = new QTimer(QCoreApplication::instance());
        s_saveTimer->setSingleShot(true);
        s_saveTimer->setInterval(500);
        QObject::connect(s_saveTimer, &QTimer::timeout, []() {
            if (s_dirty) {
                saveAppConfig();
            }
        });
    }
    s_saveTimer->start();
}

} // namespace

// ═══════════════════════════════════════════════════════════════════
// Config loading / saving
// ═══════════════════════════════════════════════════════════════════

void ensureLoaded()
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;

    try {
        QFile defaultFile(":/resources/misc/config.json");
        defaultFile.open(QIODevice::ReadOnly);
        s_defaultConfig = nlohmann::json::parse(
            defaultFile.readAll().constData()).get<AppConfigData>();

        const QString userPath = appConfigPath();
        QFile userFile(userPath);
        if (userFile.open(QIODevice::ReadOnly)) {
            nlohmann::json defaultJson = nlohmann::json(s_defaultConfig);
            nlohmann::json userJson = nlohmann::json::parse(
                userFile.readAll().constData());
            defaultJson.merge_patch(userJson);
            s_config = defaultJson.get<AppConfigData>();
            SPDLOG_INFO("config: loaded {}", userPath);
        }
        else {
            s_config = s_defaultConfig;
            SPDLOG_INFO("config: loaded defaults");
        }
    }
    catch (const nlohmann::json::exception &e) {
        SPDLOG_WARN("config: failed to load: {}", e.what());
        s_config = s_defaultConfig;
    }
}

AppConfigData &appConfig()
{
    ensureLoaded();
    return s_config;
}

bool saveAppConfig()
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
    const std::string text = nlohmann::json(s_config).dump(2);
    file.write(text.data(), static_cast<qint64>(text.size()));
    s_dirty = false;
    SPDLOG_DEBUG("config: saved {}", path);
    return true;
}

bool resetAppConfigToDefaults()
{
    ensureLoaded();
    s_config = s_defaultConfig;
    s_dirty = true;
    if (s_saveTimer) {
        s_saveTimer->stop();
    }
    SPDLOG_INFO("config: resetting user config to defaults");
    return saveAppConfig();
}

QString appConfigPath()
{
    return QDir(talkinput::appDataDir()).filePath("config.json");
}

void markConfigDirty()
{
    s_dirty = true;
    scheduleSave();
}

// ═══════════════════════════════════════════════════════════════════
// Typed helpers (thin wrappers over AppConfigData)
// ═══════════════════════════════════════════════════════════════════

nlohmann::json asrPresets()
{
    return nlohmann::json(appConfig().asrPresets);
}

nlohmann::json asrPresetById(const QString &id)
{
    const auto &presets = appConfig().asrPresets;
    auto it = presets.find(id.toStdString());
    return it != presets.end() ? nlohmann::json(it->second) : nlohmann::json::object();
}

nlohmann::json currentAsrPreset()
{
    return asrPresetById(
        QString::fromStdString(appConfig().settings.asrProviderId));
}

QString currentAsrProviderId()
{
    return QString::fromStdString(appConfig().settings.asrProviderId);
}

void setCurrentAsrProviderId(const QString &id)
{
    appConfig().settings.asrProviderId = id.toStdString();
    markConfigDirty();
}

bool isAsrPresetInstalled(const nlohmann::json &preset)
{
    const QString dirName = jsonString(preset, "modelDirName");
    if (dirName.isEmpty()) {
        return false;
    }
    const QString modelDir =
        QDir(appDataDir())
            .filePath(QStringLiteral("models/%1").arg(dirName));
    if (!QFileInfo(modelDir).isDir()) {
        return false;
    }
    const nlohmann::json files =
        preset.value("files", nlohmann::json::object());
    if (files.is_object()) {
        for (auto it = files.begin(); it != files.end(); ++it) {
            if (!it->is_string()) continue;
            const std::string key = it.key();
            const QString relative =
                QString::fromStdString(it->get<std::string>());
            const QFileInfo fi(QDir(modelDir).filePath(relative));
            if (key.size() > 4 && key.substr(key.size() - 4) == ">dir") {
                if (!fi.isDir()) return false;
            } else {
                if (!fi.isFile()) return false;
            }
        }
    }
    return true;
}

std::string currentHotwordsText()
{
    QStringList lines;
    for (const auto &item : appConfig().settings.hotwords) {
        const QString line = QString::fromStdString(item).trimmed();
        if (!line.isEmpty()) lines.append(line);
    }
    return lines.join(QLatin1Char('\n')).toStdString();
}

nlohmann::json llmPresets()
{
    return nlohmann::json(appConfig().llmPresets);
}

nlohmann::json llmProviderPreset(const QString &id)
{
    const auto &presets = appConfig().llmPresets;
    auto it = presets.find(id.toStdString());
    return it != presets.end() ? nlohmann::json(it->second) : nlohmann::json::object();
}

nlohmann::json currentLlmProviderPreset()
{
    return llmProviderPreset(
        QString::fromStdString(appConfig().settings.llmProviderId));
}

QString currentLlmProviderId()
{
    return QString::fromStdString(appConfig().settings.llmProviderId);
}

void setCurrentLlmProviderId(const QString &id)
{
    appConfig().settings.llmProviderId = id.toStdString();
    markConfigDirty();
}

void setLlmProviderSetting(const QString &id, const QString &key,
                           const nlohmann::json &value)
{
    auto &preset = appConfig().llmPresets[id.toStdString()];
    nlohmann::json j = nlohmann::json(preset);
    j[key.toStdString()] = value;
    preset = j.get<LlmPreset>();
    markConfigDirty();
}

QString llmProviderEndpoint(const nlohmann::json &provider)
{
    return jsonString(provider, "endpoint").trimmed();
}

QString llmProviderModel(const nlohmann::json &provider)
{
    return jsonString(provider, "currentModel").trimmed();
}

QString llmProviderApiKey(const nlohmann::json &provider)
{
    return jsonString(provider, "apiKey").trimmed();
}

nlohmann::json ocrPresets()
{
    return nlohmann::json(appConfig().ocrPresets);
}

nlohmann::json ocrPresetById(const QString &id)
{
    const auto &presets = appConfig().ocrPresets;
    auto it = presets.find(id.toStdString());
    return it != presets.end() ? nlohmann::json(it->second) : nlohmann::json::object();
}

nlohmann::json currentOcrPreset()
{
    return ocrPresetById(
        QString::fromStdString(appConfig().settings.ocrProviderId));
}

QString currentOcrProviderId()
{
    return QString::fromStdString(appConfig().settings.ocrProviderId);
}

void setCurrentOcrProviderId(const QString &id)
{
    appConfig().settings.ocrProviderId = id.toStdString();
    markConfigDirty();
}

nlohmann::json appConfigValue(std::string_view path,
                              const nlohmann::json &fallback)
{
    ensureLoaded();
    auto j = nlohmann::json(s_config);
    const auto pointer = nlohmann::json::json_pointer(std::string{path});
    return j.contains(pointer) ? j.at(pointer) : fallback;
}

QString appConfigString(std::string_view path, std::string_view fallback)
{
    const nlohmann::json value = appConfigValue(path, std::string{fallback});
    return value.is_string()
               ? QString::fromStdString(value.get<std::string>())
               : QString::fromUtf8(fallback.data(),
                                   static_cast<qsizetype>(fallback.size()));
}

void setAppConfigValue(std::string_view path, const nlohmann::json &value)
{
    ensureLoaded();
    auto j = nlohmann::json(s_config);
    const auto pointer = nlohmann::json::json_pointer(std::string{path});
    j[pointer] = value;
    s_config = j.get<AppConfigData>();
    s_dirty = true;
    scheduleSave();
}

// ═══════════════════════════════════════════════════════════════════
// Language
// ═══════════════════════════════════════════════════════════════════

namespace
{

QTranslator *s_appTranslator = nullptr;
QTranslator *s_qtTranslator = nullptr;

QLocale localeForLanguage(const QString &language)
{
    if (language == QStringLiteral("zh")) {
        return QLocale(QLocale::Chinese, QLocale::China);
    }
    return QLocale(QLocale::English);
}

QString normalizedLanguage(const QString &language)
{
    if (language == QStringLiteral("zh") || language == QStringLiteral("en")) {
        return language;
    }
    return systemAppLanguage();
}

void removeTranslator(QTranslator *&translator)
{
    if (!translator) {
        return;
    }
    QApplication::removeTranslator(translator);
    delete translator;
    translator = nullptr;
}

} // namespace

QString systemAppLanguage()
{
    return QLocale::system().language() == QLocale::Chinese
               ? QStringLiteral("zh")
               : QStringLiteral("en");
}

QString currentAppLanguage()
{
    const auto &lang = appConfig().settings.language;
    if (lang.empty() || lang == "system") {
        return systemAppLanguage();
    }
    return normalizedLanguage(QString::fromStdString(lang));
}

void installAppTranslations(const QString &language, QObject *parent,
                            QTranslator *&appTranslator,
                            QTranslator *&qtTranslator)
{
    removeTranslator(s_appTranslator);
    removeTranslator(s_qtTranslator);
    appTranslator = nullptr;
    qtTranslator = nullptr;

    const QString normalized = normalizedLanguage(language);
    if (normalized == QStringLiteral("en")) {
        return;
    }

    const QLocale locale = localeForLanguage(normalized);
    auto *appT = new QTranslator(parent);
    if (appT->load(locale, QStringLiteral("TalkInput"), QStringLiteral("_"),
                   QStringLiteral(":/i18n")))
    {
        SPDLOG_DEBUG("loading app translation for {}", normalized);
        s_appTranslator = appT;
        QApplication::installTranslator(s_appTranslator);
    }
    else {
        delete appT;
    }

    auto *qtT = new QTranslator(parent);
    if (qtT->load(locale, QStringLiteral("qt"), QStringLiteral("_"),
                  QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
    {
        s_qtTranslator = qtT;
        QApplication::installTranslator(s_qtTranslator);
    }
    else {
        delete qtT;
    }

    appTranslator = s_appTranslator;
    qtTranslator = s_qtTranslator;
}

} // namespace talkinput
