#pragma once

#include <QWidget>
#include <memory>

class QCheckBox;
class QEvent;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTextEdit;

namespace Ui
{
class ApiServerSettingsWidget;
}

namespace zenny
{

/// Local OpenAI-compatible API server configuration ("API 服务器").
class ApiServerSettingsWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ApiServerSettingsWidget(QWidget *parent = nullptr);
    ~ApiServerSettingsWidget() override;

    /// Re-reads the current config and refreshes every control.
    void refreshFromConfig();

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslate();
    void applyApiServerSettings();

    std::unique_ptr<Ui::ApiServerSettingsWidget> m_ui;
};

} // namespace zenny
