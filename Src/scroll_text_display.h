#pragma once

#include <QLabel>
#include <QScrollArea>
#include <QWidget>

class ScrollTextDisplay : public QWidget {
    Q_OBJECT
public:
    explicit ScrollTextDisplay(QWidget *parent = nullptr);

    void setText(const QString &text);
    void setPlaceholder(const QString &text);

private:
    QScrollArea *m_scrollArea;
    QLabel *m_label;
    QString m_placeholder;
};
