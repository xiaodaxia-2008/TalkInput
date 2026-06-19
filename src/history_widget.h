#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace talkinput
{

class RecognitionHistory;

class HistoryWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryWidget(RecognitionHistory *history,
                           QWidget *parent = nullptr);

    void refreshHistory();
    void setListening(bool listening);
    void setRealtimeText(const QString &text);
    void retranslateUi();

private:
    void editEntry(int row);
    void copyEntry(int row);
    void deleteEntry(int row);
    void clearHistory();

    RecognitionHistory *m_history = nullptr;
    QLabel *m_realtimeLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_clearButton = nullptr;
    QTableWidget *m_table = nullptr;
};

} // namespace talkinput
