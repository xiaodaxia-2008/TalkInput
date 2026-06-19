#pragma once

#include <QString>
#include <QWidget>
#include <memory>

class QEvent;

namespace Ui
{
class HistoryWidget;
}

namespace talkinput
{

class RecognitionHistory;

class HistoryWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryWidget(RecognitionHistory *history,
                           QWidget *parent = nullptr);
    ~HistoryWidget() override;

    void refreshHistory();

protected:
    void changeEvent(QEvent *event) override;

private:
    void editEntry(int row);
    void copyEntry(int row);
    void deleteEntry(int row);
    void clearHistory();

    std::unique_ptr<Ui::HistoryWidget> m_ui;
    RecognitionHistory *m_history = nullptr;
};

} // namespace talkinput
