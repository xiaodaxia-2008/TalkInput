#pragma once

#include <QLabel>
#include <QScrollArea>
#include <QWidget>
#include <memory>

namespace Ui
{
class ScrollTextDisplay;
}

class ScrollTextDisplay : public QWidget
{
    Q_OBJECT
public:
    explicit ScrollTextDisplay(QWidget *parent = nullptr);
    ~ScrollTextDisplay() override;

    void setText(const QString &text);
    void setPlaceholder(const QString &text);

protected:
    void changeEvent(QEvent *event) override;

private:
    std::unique_ptr<Ui::ScrollTextDisplay> m_ui;
    QString m_placeholder;
};
