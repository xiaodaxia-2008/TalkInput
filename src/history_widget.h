#pragma once

#include <QString>
#include <QWidget>
#include <memory>

class QEvent;

namespace Ui
{
class HistoryWidget;
}

namespace zenny
{

class RecognitionHistory;
class HistoryTableModel;

class HistoryWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryWidget(QWidget *parent = nullptr);
    ~HistoryWidget() override;

    void setHistory(RecognitionHistory *history);
    void refreshHistory();

protected:
    void changeEvent(QEvent *event) override;

private:
    void editEntry();
    void copyEntry();
    void deleteEntry();
    void clearHistory();
    int selectedRow() const;

    std::unique_ptr<Ui::HistoryWidget> m_ui;
    RecognitionHistory *m_history = nullptr;
    HistoryTableModel *m_model = nullptr;
};

} // namespace zenny
