#include "appearance_settings_widget.h"
#include "app_config.h"

#include <QCheckBox>
#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace talkinput
{

AppearanceSettingsWidget::AppearanceSettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    retranslate();
    refreshFromConfig();
}

AppearanceSettingsWidget::~AppearanceSettingsWidget() = default;

void AppearanceSettingsWidget::buildUi()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("settingsScroll"));

    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    // ── Theme ──────────────────────────────────────────────────────
    m_themeGroup = new QGroupBox(content);
    auto *themeLayout = new QVBoxLayout(m_themeGroup);
    themeLayout->setContentsMargins(16, 20, 16, 14);
    themeLayout->setSpacing(8);
    m_themeSystemRadio = new QRadioButton(m_themeGroup);
    m_themeLightRadio = new QRadioButton(m_themeGroup);
    m_themeDarkRadio = new QRadioButton(m_themeGroup);
    themeLayout->addWidget(m_themeSystemRadio);
    themeLayout->addWidget(m_themeLightRadio);
    themeLayout->addWidget(m_themeDarkRadio);
    contentLayout->addWidget(m_themeGroup);

    // ── Language ───────────────────────────────────────────────────
    m_languageGroup = new QGroupBox(content);
    auto *languageLayout = new QVBoxLayout(m_languageGroup);
    languageLayout->setContentsMargins(16, 20, 16, 14);
    languageLayout->setSpacing(8);
    m_languageChineseRadio = new QRadioButton(m_languageGroup);
    m_languageEnglishRadio = new QRadioButton(m_languageGroup);
    languageLayout->addWidget(m_languageChineseRadio);
    languageLayout->addWidget(m_languageEnglishRadio);
    contentLayout->addWidget(m_languageGroup);

    // ── Startup ────────────────────────────────────────────────────
    m_startupGroup = new QGroupBox(content);
    auto *startupLayout = new QHBoxLayout(m_startupGroup);
    startupLayout->setContentsMargins(16, 20, 16, 14);
    m_startMinimizedCheck = new QCheckBox(m_startupGroup);
    startupLayout->addWidget(m_startMinimizedCheck);
    contentLayout->addWidget(m_startupGroup);

    contentLayout->addStretch();

    scroll->setWidget(content);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

    auto emitTheme = [this](ThemeMode mode) {
        if (mode == ThemeMode::System) {
            m_themeSystemRadio->setChecked(true);
        }
        else if (mode == ThemeMode::Light) {
            m_themeLightRadio->setChecked(true);
        }
        else {
            m_themeDarkRadio->setChecked(true);
        }
        emit themeChanged(mode);
    };
    connect(m_themeSystemRadio, &QRadioButton::toggled, this,
            [emitTheme](bool checked) {
                if (checked) {
                    emitTheme(ThemeMode::System);
                }
            });
    connect(m_themeLightRadio, &QRadioButton::toggled, this,
            [emitTheme](bool checked) {
                if (checked) {
                    emitTheme(ThemeMode::Light);
                }
            });
    connect(m_themeDarkRadio, &QRadioButton::toggled, this,
            [emitTheme](bool checked) {
                if (checked) {
                    emitTheme(ThemeMode::Dark);
                }
            });

    connect(m_languageChineseRadio, &QRadioButton::toggled, this,
            [this](bool checked) {
                if (checked) {
                    emit languageChanged(QStringLiteral("zh"));
                }
            });
    connect(m_languageEnglishRadio, &QRadioButton::toggled, this,
            [this](bool checked) {
                if (checked) {
                    emit languageChanged(QStringLiteral("en"));
                }
            });

    connect(m_startMinimizedCheck, &QCheckBox::toggled, this, [](bool checked) {
        appConfig().settings.startMinimized = checked;
        markConfigDirty();
    });
}

void AppearanceSettingsWidget::retranslate()
{
    m_themeGroup->setTitle(tr("Theme"));
    m_themeSystemRadio->setText(tr("Follow system"));
    m_themeLightRadio->setText(tr("Light"));
    m_themeDarkRadio->setText(tr("Dark"));

    m_languageGroup->setTitle(tr("Language"));
    m_languageChineseRadio->setText(tr("Chinese (简体中文)"));
    m_languageEnglishRadio->setText(tr("English"));

    m_startupGroup->setTitle(tr("Startup"));
    m_startMinimizedCheck->setText(tr("Start minimized"));
}

void AppearanceSettingsWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
}

void AppearanceSettingsWidget::refreshFromConfig()
{
    {
        const QSignalBlocker b1(m_themeSystemRadio);
        const QSignalBlocker b2(m_themeLightRadio);
        const QSignalBlocker b3(m_themeDarkRadio);
        switch (themeModeFromString(appConfig().settings.theme)) {
        case ThemeMode::Light:
            m_themeLightRadio->setChecked(true);
            break;
        case ThemeMode::Dark:
            m_themeDarkRadio->setChecked(true);
            break;
        case ThemeMode::System:
            m_themeSystemRadio->setChecked(true);
            break;
        }
    }

    {
        const QSignalBlocker b1(m_languageChineseRadio);
        const QSignalBlocker b2(m_languageEnglishRadio);
        if (currentAppLanguage() == QStringLiteral("en")) {
            m_languageEnglishRadio->setChecked(true);
        }
        else {
            m_languageChineseRadio->setChecked(true);
        }
    }

    {
        const QSignalBlocker blocker(m_startMinimizedCheck);
        m_startMinimizedCheck->setChecked(appConfig().settings.startMinimized);
    }
}

} // namespace talkinput
