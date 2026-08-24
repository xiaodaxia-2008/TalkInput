#pragma once

#include <QWidget>
#include <memory>

class QCheckBox;
class QEvent;

namespace Ui
{
class RecognitionBehaviorWidget;
}

namespace talkinput
{

/// Speech recognition behavior checkboxes ("识别行为").
class RecognitionBehaviorWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit RecognitionBehaviorWidget(QWidget *parent = nullptr);
    ~RecognitionBehaviorWidget() override;

    /// Re-reads the current config and refreshes every control.
    void refreshFromConfig();

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslate();

    std::unique_ptr<Ui::RecognitionBehaviorWidget> m_ui;
};

} // namespace talkinput
