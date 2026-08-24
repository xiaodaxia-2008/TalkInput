#include "appearance_settings_widget.h"
#include "app_config.h"
#include "ui_appearance_settings_widget.h"

#include <QCheckBox>
#include <QEvent>
#include <QRadioButton>
#include <QSignalBlocker>

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
    m_ui = std::make_unique<Ui::AppearanceSettingsWidget>();
    m_ui->setupUi(this);

    const auto emitTheme = [this](ThemeMode mode) {
        if (mode == ThemeMode::System) {
            m_ui->themeSystemRadio->setChecked(true);
        }
        else if (mode == ThemeMode::Light) {
            m_ui->themeLightRadio->setChecked(true);
        }
        else {
            m_ui->themeDarkRadio->setChecked(true);
        }
        emit themeChanged(mode);
    };
    connect(m_ui->themeSystemRadio, &QRadioButton::toggled, this,
            [emitTheme](bool checked) {
                if (checked) {
                    emitTheme(ThemeMode::System);
                }
            });
    connect(m_ui->themeLightRadio, &QRadioButton::toggled, this,
            [emitTheme](bool checked) {
                if (checked) {
                    emitTheme(ThemeMode::Light);
                }
            });
    connect(m_ui->themeDarkRadio, &QRadioButton::toggled, this,
            [emitTheme](bool checked) {
                if (checked) {
                    emitTheme(ThemeMode::Dark);
                }
            });
    connect(m_ui->languageChineseRadio, &QRadioButton::toggled, this,
            [this](bool checked) {
                if (checked) {
                    emit languageChanged(QStringLiteral("zh"));
                }
            });
    connect(m_ui->languageEnglishRadio, &QRadioButton::toggled, this,
            [this](bool checked) {
                if (checked) {
                    emit languageChanged(QStringLiteral("en"));
                }
            });
    connect(m_ui->startMinimizedCheck, &QCheckBox::toggled, this,
            [](bool checked) {
                appConfig().settings.startMinimized = checked;
                markConfigDirty();
            });
}

void AppearanceSettingsWidget::retranslate()
{
    m_ui->retranslateUi(this);
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
        const QSignalBlocker b1(m_ui->themeSystemRadio);
        const QSignalBlocker b2(m_ui->themeLightRadio);
        const QSignalBlocker b3(m_ui->themeDarkRadio);
        switch (themeModeFromString(appConfig().settings.theme)) {
        case ThemeMode::Light:
            m_ui->themeLightRadio->setChecked(true);
            break;
        case ThemeMode::Dark:
            m_ui->themeDarkRadio->setChecked(true);
            break;
        case ThemeMode::System:
            m_ui->themeSystemRadio->setChecked(true);
            break;
        }
    }
    {
        const QSignalBlocker b1(m_ui->languageChineseRadio);
        const QSignalBlocker b2(m_ui->languageEnglishRadio);
        if (currentAppLanguage() == QStringLiteral("en")) {
            m_ui->languageEnglishRadio->setChecked(true);
        }
        else {
            m_ui->languageChineseRadio->setChecked(true);
        }
    }
    const QSignalBlocker blocker(m_ui->startMinimizedCheck);
    m_ui->startMinimizedCheck->setChecked(appConfig().settings.startMinimized);
}

} // namespace talkinput
