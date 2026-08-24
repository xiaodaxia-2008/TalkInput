#pragma once

#include <QPlainTextEdit>
#include <QWidget>
#include <memory>

namespace Ui
{
class LogPanel;
}

namespace talkinput
{

class LogPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit LogPanel(QWidget *parent = nullptr);
    ~LogPanel() override;

    QPlainTextEdit *textEdit() const;

private:
    void onClear();

    std::unique_ptr<Ui::LogPanel> m_ui;
    static constexpr int kMaxLines = 10000;
};

} // namespace talkinput
