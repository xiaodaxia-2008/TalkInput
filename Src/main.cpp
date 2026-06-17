#include "main_window.h"

#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QMessageLogContext>
#include <QSettings>
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
    QApplication app(argc, argv);
    QApplication::setApplicationName("TalkInput");
    QApplication::setApplicationDisplayName("TalkInput Voice Input");
    QApplication::setApplicationVersion(PROJECT_VERSION_STR);
    QApplication::setOrganizationName("TalkInput");
    QApplication::setWindowIcon(QIcon(":/resources/icon.png"));

    QSettings s;

    const bool startHidden =
        s.value(QStringLiteral("app/startMinimized"), false).toBool();
    const QString lang =
        s.value(QStringLiteral("app/language"), QStringLiteral("zh"))
            .toString();

    if (lang == QStringLiteral("zh")) {
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

    talkinput::MainWindow window;
    if (!startHidden) {
        window.show();
    }

    return app.exec();
}
