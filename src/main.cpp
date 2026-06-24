#include "app_config.h"
#include "logging.h"
#include "main_window.h"

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);
    spdlog::default_logger()->set_level(spdlog::level::debug);
    SPDLOG_DEBUG("starting application");

    QApplication app(argc, argv);
    SPDLOG_DEBUG("QApplication created");
    QApplication::setApplicationName("TalkInput");
    QApplication::setApplicationDisplayName("TalkInput Voice Input");
    QApplication::setApplicationVersion(PROJECT_VERSION_STR);
    QApplication::setOrganizationName("ZenShawn");
    QApplication::setWindowIcon(QIcon(":/resources/icons/icon.png"));
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &talkinput::saveAppConfig);

    talkinput::initLogger();
    SPDLOG_DEBUG("file logger initialized");

    QFile styleFile(":/resources/misc/app.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }
    else {
        SPDLOG_WARN("failed to load application stylesheet");
    }

    SPDLOG_DEBUG("config path {}", talkinput::appConfigPath());

    try {
        const bool startHidden = talkinput::appConfig().settings.startMinimized;

        SPDLOG_DEBUG("constructing MainWindow");
        talkinput::MainWindow window;
        SPDLOG_DEBUG("MainWindow constructed");
        if (!startHidden) {
            SPDLOG_DEBUG("showing MainWindow");
            window.show();
        }
        else {
            SPDLOG_DEBUG("start hidden is enabled");
        }

        SPDLOG_DEBUG("entering event loop");
        return app.exec();
    }
    catch (const std::exception &e) {
        SPDLOG_ERROR("exception during startup: {}", e.what());
        const auto result = QMessageBox::critical(
            nullptr, QStringLiteral("TalkInput"),
            QObject::tr("An error occurred: %1\n\n"
                        "Reset configuration to defaults?")
                .arg(e.what()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (result == QMessageBox::Yes) {
            talkinput::resetAppConfigToDefaults();
            QMessageBox::information(
                nullptr, QStringLiteral("TalkInput"),
                QObject::tr("Configuration has been reset. "
                            "Please restart the application."));
        }
        return 1;
    }
}
