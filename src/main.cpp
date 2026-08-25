#include "app_config.h"
#include "local_ai_api_server.h"
#include "logging.h"
#include "main_window.h"
#include "single_instance.h"
#include "theme.h"

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    spdlog::set_level(spdlog::level::debug);
    spdlog::default_logger()->set_level(spdlog::level::debug);
    SPDLOG_DEBUG("starting application");

    QApplication app(argc, argv);
    QFont appFont = app.font();
    appFont.setPointSizeF(appFont.pointSizeF() + 1.0);
    app.setFont(appFont);
    SPDLOG_DEBUG("QApplication created");
    QApplication::setApplicationName("Zenny");
    QApplication::setApplicationDisplayName("Zenny Voice Input");
    QApplication::setApplicationVersion(PROJECT_VERSION_STR);
    QApplication::setOrganizationName("ZenShawn");
    QApplication::setWindowIcon(QIcon(":/resources/icons/icon.png"));

    zenny::SingleInstance singleInstance(QStringLiteral("Zenny"));
    if (!singleInstance.start()) {
        return 0;
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     &zenny::saveAppConfig);

    zenny::initLogger();
    SPDLOG_DEBUG("file logger initialized");

    SPDLOG_DEBUG("config path {}", zenny::appConfigPath());

    try {
        // Load the user config eagerly so the queued applySettings() on the API
        // server thread never reads uninitialized defaults (e.g. an API server
        // that is silently left disabled at startup).
        (void)zenny::appConfig();

        // Apply the configured theme before any window is shown.
        zenny::applyTheme(zenny::themeModeFromString(
            zenny::appConfig().settings.theme));

        // Owned before MainWindow so VoicePipelineController (created inside
        // MainWindow) is guaranteed to outlive any in-flight API request.
        zenny::LocalAiApiServer apiServer;
        apiServer.applySettings();

        const bool startHidden = zenny::appConfig().settings.startMinimized;

        SPDLOG_DEBUG("constructing MainWindow");
        zenny::MainWindow window;
        QObject::connect(&singleInstance,
                         &zenny::SingleInstance::activationRequested,
                         &window, [&window]() {
                             window.showNormal();
                             window.raise();
                             window.activateWindow();
                         });
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
            nullptr, QStringLiteral("Zenny"),
            QObject::tr("An error occurred: %1\n\n"
                        "Reset configuration to defaults?")
                .arg(e.what()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (result == QMessageBox::Yes) {
            zenny::resetAppConfigToDefaults();
            QMessageBox::information(
                nullptr, QStringLiteral("Zenny"),
                QObject::tr("Configuration has been reset. "
                            "Please restart the application."));
        }
        return 1;
    }
}
