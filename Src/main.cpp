#include "logging.h"
#include "main_window.h"
#include "utils.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::debug);
    spdlog::debug("main: starting application");

    QApplication app(argc, argv);
    spdlog::debug("main: QApplication created");
    QApplication::setApplicationName("TalkInput");
    QApplication::setApplicationDisplayName("TalkInput Voice Input");
    QApplication::setApplicationVersion(PROJECT_VERSION_STR);
    QApplication::setOrganizationName("TalkInput");
    QApplication::setWindowIcon(
        talkinput::resourceIcon(":/resources/icon.png"));

    QFile styleFile(":/resources/app.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }
    else {
        spdlog::warn("main: failed to load application stylesheet");
    }

    QString settingsDir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (settingsDir.isEmpty()) {
        settingsDir = QDir::home().filePath(".config/TalkInput");
    }
    QDir().mkpath(settingsDir);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
    spdlog::debug("main: settings directory set to {}", settingsDir);

    QSettings s;

    const bool startHidden =
        s.value(QStringLiteral("app/startMinimized"), false).toBool();
    const QString lang =
        s.value(QStringLiteral("app/language"), QStringLiteral("zh"))
            .toString();

    if (lang == QStringLiteral("zh")) {
        spdlog::debug("main: loading Chinese translations");
        auto *appTranslator = new QTranslator(&app);
        if (appTranslator->load(QStringLiteral(":/i18n/TalkInput_zh.qm"))) {
            app.installTranslator(appTranslator);
        }
        else {
            delete appTranslator;
        }

        auto *qtTranslator = new QTranslator(&app);
        if (qtTranslator->load(
                QStringLiteral("qt_zh_CN"),
                QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        {
            app.installTranslator(qtTranslator);
        }
        else {
            delete qtTranslator;
        }
    }

    spdlog::debug("main: constructing MainWindow");
    talkinput::MainWindow window;
    spdlog::debug("main: MainWindow constructed");
    if (!startHidden) {
        spdlog::debug("main: showing MainWindow");
        window.show();
    }
    else {
        spdlog::debug("main: start hidden is enabled");
    }

    spdlog::debug("main: entering event loop");
    return app.exec();
}
