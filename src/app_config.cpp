#include "app_config.h"
#include "json_utils.h"
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
