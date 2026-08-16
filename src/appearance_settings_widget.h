#pragma once

#include "theme.h"

#include <QWidget>
#include <memory>

class QCheckBox;
class QEvent;
class QGroupBox;
class QRadioButton;

namespace talkinput
{

/// Theme, language, and startup options ("外观").
class AppearanceSettingsWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit AppearanceSettingsWidget(QWidget *parent = nullptr);
    ~AppearanceSettingsWidget() override;

    /// Re-reads the current config and refreshes every control.
    void refreshFromConfig();

signals:
    void themeChanged(ThemeMode mode);
    void languageChanged(const QString &language);

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslate();

    QGroupBox *m_themeGroup = nullptr;
    QRadioButton *m_themeSystemRadio = nullptr;
    QRadioButton *m_themeLightRadio = nullptr;
    QRadioButton *m_themeDarkRadio = nullptr;
    QGroupBox *m_languageGroup = nullptr;
    QRadioButton *m_languageChineseRadio = nullptr;
    QRadioButton *m_languageEnglishRadio = nullptr;
    QGroupBox *m_startupGroup = nullptr;
    QCheckBox *m_startMinimizedCheck = nullptr;
};

} // namespace talkinput
