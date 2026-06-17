#include "logging.h"
#include "main_window.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLibraryInfo>
#include <QMessageLogContext>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>
#include <QtLogging>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static void messageHandler(QtMsgType type, const QMessageLogContext &ctx,
                           const QString &msg)
{
    Q_UNUSED(ctx);
    QString prefix;
    switch (type) {
    case QtWarningMsg:
        prefix = QStringLiteral("WARNING: ");
        break;
    case QtCriticalMsg:
        prefix = QStringLiteral("CRITICAL: ");
        break;
    case QtFatalMsg:
        prefix = QStringLiteral("FATAL: ");
        break;
    default:
        break;
    }
    QString out = prefix + msg + QStringLiteral("\n");
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode;
    if (h && h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        WriteConsoleW(h, out.utf16(), out.length(), nullptr, nullptr);
    }
    else {
        fprintf(stderr, "%s", out.toLocal8Bit().constData());
    }
    if (type == QtFatalMsg) {
        abort();
    }
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(messageHandler);
    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::debug);
    spdlog::debug("main: starting application");

    QApplication app(argc, argv);
    spdlog::debug("main: QApplication created");
    QApplication::setApplicationName("TalkInput");
    QApplication::setApplicationDisplayName("TalkInput Voice Input");
    QApplication::setApplicationVersion(PROJECT_VERSION_STR);
    QApplication::setOrganizationName("TalkInput");
    QApplication::setWindowIcon(QIcon(":/resources/icon.png"));

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
