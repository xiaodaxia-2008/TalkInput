#pragma once

#include "theme.h"

#include <QWidget>
#include <memory>

class QCheckBox;
class QEvent;
class QGroupBox;
class QRadioButton;

namespace Ui
{
class AppearanceSettingsWidget;
}

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

    std::unique_ptr<Ui::AppearanceSettingsWidget> m_ui;
};

} // namespace talkinput
