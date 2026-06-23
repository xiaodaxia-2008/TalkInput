#pragma once

#include <QPlainTextEdit>
#include <QWidget>

namespace talkinput
{

class LogPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit LogPanel(QWidget *parent = nullptr);
    ~LogPanel() override;

    QPlainTextEdit *textEdit() const
    {
        return m_textEdit;
    }

private:
    void onClear();

    QPlainTextEdit *m_textEdit = nullptr;
    static constexpr int kMaxLines = 10000;
};

} // namespace talkinput
