#include "theme.h"
#include "logging.h"

#include <QApplication>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyleHints>
#include <QSvgRenderer>

namespace zenny
{

ThemeMode themeModeFromString(const std::string &s)
{
    if (s == "dark") {
        return ThemeMode::Dark;
    }
    if (s == "light") {
        return ThemeMode::Light;
    }
    return ThemeMode::System;
}

std::string themeModeToString(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::Dark:
        return "dark";
    case ThemeMode::Light:
        return "light";
    case ThemeMode::System:
        return "system";
    }
    return "system";
}

bool isDarkTheme(ThemeMode mode)
{
    if (mode == ThemeMode::Dark) {
        return true;
    }
    if (mode == ThemeMode::Light) {
        return false;
    }
    return QGuiApplication::styleHints()->colorScheme() ==
           Qt::ColorScheme::Dark;
}

namespace
{

QPalette buildPalette(bool dark)
{
    QPalette p;
    if (dark) {
        p.setColor(QPalette::Window, QColor(0x1f, 0x1f, 0x1f));
        p.setColor(QPalette::WindowText, QColor(0xe6, 0xe6, 0xe6));
        p.setColor(QPalette::Base, QColor(0x26, 0x26, 0x26));
        p.setColor(QPalette::AlternateBase, QColor(0x2b, 0x2b, 0x2b));
        p.setColor(QPalette::ToolTipBase, QColor(0x2d, 0x2d, 0x2d));
        p.setColor(QPalette::ToolTipText, QColor(0xe6, 0xe6, 0xe6));
        p.setColor(QPalette::Text, QColor(0xe6, 0xe6, 0xe6));
        p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8a, 0x8a));
        p.setColor(QPalette::Button, QColor(0x2f, 0x2f, 0x2f));
        p.setColor(QPalette::ButtonText, QColor(0xe6, 0xe6, 0xe6));
        p.setColor(QPalette::BrightText, QColor(0xff, 0x52, 0x4d));
        p.setColor(QPalette::Link, QColor(0x4d, 0xa3, 0xff));
        p.setColor(QPalette::Highlight, QColor(0x1e, 0x5f, 0xb0));
        p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::Disabled, QPalette::Text,
                   QColor(0x6a, 0x6a, 0x6a));
        p.setColor(QPalette::Disabled, QPalette::WindowText,
                   QColor(0x6a, 0x6a, 0x6a));
        p.setColor(QPalette::Disabled, QPalette::ButtonText,
                   QColor(0x6a, 0x6a, 0x6a));
    }
    else {
        p.setColor(QPalette::Window, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::WindowText, QColor(0x1f, 0x1f, 0x1f));
        p.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::AlternateBase, QColor(0xf5, 0xf5, 0xf5));
        p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::ToolTipText, QColor(0x1f, 0x1f, 0x1f));
        p.setColor(QPalette::Text, QColor(0x1f, 0x1f, 0x1f));
        p.setColor(QPalette::PlaceholderText, QColor(0x8a, 0x8a, 0x8a));
        p.setColor(QPalette::Button, QColor(0xf5, 0xf5, 0xf5));
        p.setColor(QPalette::ButtonText, QColor(0x1f, 0x1f, 0x1f));
        p.setColor(QPalette::BrightText, QColor(0xc0, 0x39, 0x2b));
        p.setColor(QPalette::Link, QColor(0x00, 0x78, 0xd4));
        p.setColor(QPalette::Highlight, QColor(0x00, 0x78, 0xd4));
        p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    }
    return p;
}

} // namespace

bool applyTheme(ThemeMode mode)
{
    const bool dark = isDarkTheme(mode);
    QApplication::setPalette(buildPalette(dark));

    const QString qssPath =
        dark ? QStringLiteral(":/resources/misc/app-dark.qss")
             : QStringLiteral(":/resources/misc/app.qss");
    QFile styleFile(qssPath);
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }
    else {
        SPDLOG_WARN("failed to load application stylesheet {}", qssPath);
    }

    SPDLOG_DEBUG("applied theme: {}", dark ? "dark" : "light");
    return dark;
}

QIcon themedNavIcon(const QString &svgPath, bool dark)
{
    QFile file(svgPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QIcon(svgPath);
    }

    QByteArray svg = file.readAll();
    const QByteArray stroke = dark ? QByteArray("#e0e0e0") : QByteArray("#333");
    svg.replace("#333", stroke);

    QSvgRenderer renderer(svg);
    const int logicalSize = 32;
    QPixmap pixmap(logicalSize * 2, logicalSize * 2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    renderer.render(&painter, QRect(0, 0, logicalSize * 2, logicalSize * 2));
    painter.end();
    pixmap.setDevicePixelRatio(2.0);
    return QIcon(pixmap);
}

} // namespace zenny
