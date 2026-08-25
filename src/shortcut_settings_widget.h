#pragma once

#include <QWidget>
#include <memory>

class QEvent;
namespace Ui
{
class ShortcutSettingsWidget;
}

namespace zenny
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

    std::unique_ptr<Ui::ShortcutSettingsWidget> m_ui;
};

} // namespace zenny
