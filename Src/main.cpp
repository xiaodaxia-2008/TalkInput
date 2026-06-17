#include "main_window.h"

#include <QApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QSettings>
#include <QTranslator>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8);
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
