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
// Config loading / saving
// ═══════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════
// Typed config accessors
// ═══════════════════════════════════════════════════════════════════

namespace
{

AppConfigData s_typedConfig;

} // namespace

const AppConfigData &appConfig()
{
    ensureLoaded();
    if (s_config != nlohmann::json::object() && s_typedConfig.asrPresets.empty()) {
        s_typedConfig = s_config.get<AppConfigData>();
    }
    return s_typedConfig;
}

void setAppConfig(const AppConfigData &config)
{
    s_typedConfig = config;
    s_config = nlohmann::json(config);
    s_dirty = true;
    scheduleSave();
}

// ═══════════════════════════════════════════════════════════════════
// JSON serialization
// ═══════════════════════════════════════════════════════════════════

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AsrPresetParams, sampleRate, featureDim,
                                   numThreads, modelingUnit, hotwordsScore,
                                   language, senseVoiceUseItn,
                                   funasrSystemPrompt, funasrUserPrompt,
                                   funasrMaxNewTokens, funasrTemperature,
                                   funasrTopP, funasrSeed, funasrItn)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AsrPreset, id, name, type, languages,
                                   modelDirName, url, size, paramCount,
                                   streamingSupport, hotwordsSupport, params,
                                   files)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LlmModelPrice, inputPer1M, outputPer1M,
                                   cacheHitInputPer1M, cacheMissInputPer1M)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LlmModel, name, url, fileName, size, price)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LlmPreset, id, name, endpoint, apiKey,
                                   currentModel, models, managedLocalService,
                                   localServicePort, localServiceMaxHealthAttempts,
                                   localServiceArchiveUrl)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(OcrPreset, id, name, type)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AppSettings::App, language, startMinimized)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AppSettings::Asr, providerId)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AppSettings::Ocr, providerId)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AppSettings::Llm, providerId, systemPrompt,
                                   userPrompt)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AppSettings::Hotkeys, asr, asrLlm, asrLlmOcr)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AppSettings, app, hotwords, asr, ocr, llm,
                                   hotkeys)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AppConfigData, settings, asrPresets,
                                   llmPresets, ocrPresets)

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
    const QString configured =
        appConfigString("/settings/app/language").trimmed();
    if (configured.isEmpty() || configured == QStringLiteral("system")) {
        return systemAppLanguage();
    }
    return normalizedLanguage(configured);
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

// ═══════════════════════════════════════════════════════════════════
// ASR presets / config
// ═══════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════
// LLM presets / config
// ═══════════════════════════════════════════════════════════════════

nlohmann::json llmPresets()
{
    return appConfigValue("/llmPresets");
}

nlohmann::json firstLlmProviderPreset()
{
    const nlohmann::json providers = llmPresets();
    if (providers.is_object() && !providers.empty()) {
        return providers.begin().value();
    }
    return nlohmann::json::object();
}

nlohmann::json llmProviderPreset(const QString &id)
{
    if (id.isEmpty()) {
        return nlohmann::json::object();
    }
    return appConfigValue(("/llmPresets/" + id).toStdString());
}

QString currentLlmProviderId()
{
    return appConfigString("/settings/llm/providerId").trimmed();
}

nlohmann::json currentLlmProviderPreset()
{
    const nlohmann::json provider = llmProviderPreset(currentLlmProviderId());
    if (provider.is_object() && !provider.empty()) {
        return provider;
    }
    return firstLlmProviderPreset();
}

void setCurrentLlmProviderId(const QString &id)
{
    setAppConfigValue("/settings/llm/providerId", id.toStdString());
}

void setLlmProviderSetting(const QString &id, const QString &key,
                           const nlohmann::json &value)
{
    if (id.isEmpty() || key.isEmpty()) {
        return;
    }

    setAppConfigValue(
        QStringLiteral("/llmPresets/%1/%2").arg(id, key).toStdString(), value);
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

bool llmProviderUsesManagedLocalService(const nlohmann::json &provider)
{
    return provider.value("managedLocalService", false);
}

// ═══════════════════════════════════════════════════════════════════
// OCR presets / config
// ═══════════════════════════════════════════════════════════════════

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
