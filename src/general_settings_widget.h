#pragma once

#include <QWidget>
#include <memory>

class QEvent;
class QGroupBox;
class QPushButton;

namespace talkinput
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

    QGroupBox *m_group = nullptr;
    QPushButton *m_resetButton = nullptr;
    QPushButton *m_dataDirectoryButton = nullptr;
    QPushButton *m_aboutButton = nullptr;
    QPushButton *m_exitButton = nullptr;
};

} // namespace talkinput
