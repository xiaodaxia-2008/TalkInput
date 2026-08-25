#pragma once

#include <QWidget>

class QLabel;
class QScrollArea;

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
    QLabel *m_scrollTextLabel = nullptr;
    QScrollArea *m_scrollTextArea = nullptr;
    QString m_placeholder;
};
