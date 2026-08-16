#pragma once

#include <QWidget>
#include <memory>

class QCheckBox;
class QEvent;
class QGroupBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace talkinput
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

    QGroupBox *m_group = nullptr;
    QCheckBox *m_enableCheck = nullptr;
    QLabel *m_hostLabel = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QLabel *m_portLabel = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QLabel *m_keyLabel = nullptr;
    QLineEdit *m_keyEdit = nullptr;
};

} // namespace talkinput
