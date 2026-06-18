#include "app_config.h"
#include "logging.h"
#include "main_window.h"
#include "utils.h"

#include <QApplication>
#include <QFile>
#include <QLibraryInfo>
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
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &talkinput::saveAppConfig);

    QFile styleFile(":/resources/app.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }
    else {
        spdlog::warn("main: failed to load application stylesheet");
    }

    spdlog::debug("main: config path {}", talkinput::appConfigPath());

    const bool startHidden =
        talkinput::appConfigBool("settings/app/startMinimized", false);
    const QString lang =
        talkinput::appConfigString("settings/app/language", "zh");

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
