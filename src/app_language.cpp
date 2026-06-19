#include "app_language.h"
#include "app_config.h"
#include "logging.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace talkinput
{
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

} // namespace talkinput
