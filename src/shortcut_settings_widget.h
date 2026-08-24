#pragma once

#include <QWidget>
#include <memory>

class QEvent;
class QGroupBox;
class QKeySequenceEdit;
class QLabel;
class QPushButton;

namespace talkinput
{

/// Global shortcuts and active pipeline mode ("快捷键").
class ShortcutSettingsWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit ShortcutSettingsWidget(QWidget *parent = nullptr);
    ~ShortcutSettingsWidget() override;

    /// Re-reads the current config and refreshes every control.
    void refreshFromConfig();

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslate();
    void initShortcuts();

    QGroupBox *m_group = nullptr;
    QLabel *m_triggerLabel = nullptr;
    QKeySequenceEdit *m_triggerEdit = nullptr;
    QPushButton *m_triggerApplyBtn = nullptr;
    QLabel *m_modeSwitchLabel = nullptr;
    QKeySequenceEdit *m_modeSwitchEdit = nullptr;
    QPushButton *m_modeSwitchApplyBtn = nullptr;
};

} // namespace talkinput
