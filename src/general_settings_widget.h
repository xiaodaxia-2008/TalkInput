#pragma once

#include <QWidget>
#include <memory>

class QEvent;
class QGroupBox;
class QPushButton;

namespace Ui
{
class GeneralSettingsWidget;
}

namespace zenny
{

/// Misc actions: reset, data directory, about, exit ("常规").
class GeneralSettingsWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit GeneralSettingsWidget(QWidget *parent = nullptr);
    ~GeneralSettingsWidget() override;

signals:
    void resetSettingsRequested();
    void openDataDirectoryRequested();
    void aboutRequested();
    void exitRequested();

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslate();

    std::unique_ptr<Ui::GeneralSettingsWidget> m_ui;
};

} // namespace zenny
