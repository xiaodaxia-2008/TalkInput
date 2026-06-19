#include "app_config.h"
#include "app_language.h"
#include "logging.h"
#include "main_window.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QTranslator>

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::debug);
    SPDLOG_DEBUG("main: starting application");

    QApplication app(argc, argv);
    SPDLOG_DEBUG("main: QApplication created");
    QApplication::setApplicationName("TalkInput");
    QApplication::setApplicationDisplayName("TalkInput Voice Input");
    QApplication::setApplicationVersion(PROJECT_VERSION_STR);
    QApplication::setOrganizationName("ZenShawn");
    QApplication::setWindowIcon(QIcon(":/resources/icons/icon.png"));
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &talkinput::saveAppConfig);

    QFile styleFile(":/resources/misc/app.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }
    else {
        SPDLOG_WARN("main: failed to load application stylesheet");
    }

    SPDLOG_DEBUG("main: config path {}", talkinput::appConfigPath());

    const bool startHidden =
        talkinput::appConfigBool("/settings/app/startMinimized", false);
    QTranslator *appTranslator = nullptr;
    QTranslator *qtTranslator = nullptr;
    talkinput::installAppTranslations(talkinput::currentAppLanguage(), &app,
                                      appTranslator, qtTranslator);

    SPDLOG_DEBUG("main: constructing MainWindow");
    talkinput::MainWindow window;
    SPDLOG_DEBUG("main: MainWindow constructed");
    if (!startHidden) {
        SPDLOG_DEBUG("main: showing MainWindow");
        window.show();
    }
    else {
        SPDLOG_DEBUG("main: start hidden is enabled");
    }

    SPDLOG_DEBUG("main: entering event loop");
    return app.exec();
}
