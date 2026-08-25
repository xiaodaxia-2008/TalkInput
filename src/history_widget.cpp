#include "history_widget.h"
#include "logging.h"
#include "recognition_history.h"
#include "ui_history_edit_dialog.h"
#include "ui_history_widget.h"
#include "utils.h"

#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTableView>
#include <QTextEdit>
#include <QTextOption>
#include <QVBoxLayout>
#include <QVector>

namespace zenny
{

class HistoryTableModel final : public QAbstractTableModel
{
public:
    explicit HistoryTableModel(RecognitionHistory *history, QObject *parent)
        : QAbstractTableModel(parent), m_history(history)
    {
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_entries.size();
    }

    int columnCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : 1;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.column() != 0 ||
            index.row() >= m_entries.size())
        {
            return {};
        }

        const auto &entry = m_entries.at(index.row());
        if (role == Qt::DisplayRole) {
            QString display = entry.text;
            if (display.length() > 55) {
                display += QStringLiteral("...");
                display.truncate(58);
            }
            return display;
        }
        if (role == Qt::ToolTipRole) {
            return entry.text;
        }
        if (role == Qt::UserRole) {
            return entry.id;
        }
        return {};
    }

    bool canFetchMore(const QModelIndex &parent = {}) const override
    {
        return !parent.isValid() && m_hasMore;
    }

    void fetchMore(const QModelIndex &parent = {}) override
    {
        if (parent.isValid() || !m_hasMore || !m_history) {
            return;
        }

        constexpr int pageSize = 100;
        auto page = m_history->entries(m_entries.size(), pageSize + 1);
        m_hasMore = page.size() > pageSize;
        if (m_hasMore) {
            page.removeLast();
        }
        if (page.isEmpty()) {
            return;
        }

        const int first = m_entries.size();
        const int last = first + page.size() - 1;
        beginInsertRows({}, first, last);
        m_entries += page;
        endInsertRows();
    }

    const RecognitionHistory::Entry *entryAt(int row) const
    {
        if (row < 0 || row >= m_entries.size()) {
            return nullptr;
        }
        return &m_entries.at(row);
    }

    void reload()
    {
        beginResetModel();
        m_entries.clear();
        m_hasMore = true;
        endResetModel();
        fetchMore();
    }

    void setHistory(RecognitionHistory *history)
    {
        m_history = history;
        reload();
    }

private:
    RecognitionHistory *m_history = nullptr;
    QVector<RecognitionHistory::Entry> m_entries;
    bool m_hasMore = true;
};

HistoryWidget::HistoryWidget(QWidget *parent)
    : QWidget(parent), m_ui(std::make_unique<Ui::HistoryWidget>())
{
    SPDLOG_DEBUG("HistoryWidget: constructor begin");
    m_ui->setupUi(this);
    m_model = new HistoryTableModel(m_history, this);
    m_ui->table->setModel(m_model);
    connect(m_ui->clearButton, &QPushButton::clicked, this,
            &HistoryWidget::clearHistory);
    connect(m_ui->editButton, &QPushButton::clicked, this,
            &HistoryWidget::editEntry);
    connect(m_ui->copyButton, &QPushButton::clicked, this,
            &HistoryWidget::copyEntry);
    connect(m_ui->deleteButton, &QPushButton::clicked, this,
            &HistoryWidget::deleteEntry);
    const auto updateActionButtons = [this]() {
        const QModelIndexList selectedRows =
            m_ui->table->selectionModel()->selectedRows();
        m_ui->editButton->setEnabled(selectedRows.size() == 1);
        m_ui->copyButton->setEnabled(!selectedRows.isEmpty());
        m_ui->deleteButton->setEnabled(!selectedRows.isEmpty());
    };
    connect(
        m_ui->table->selectionModel(), &QItemSelectionModel::selectionChanged,
        this,
        [updateActionButtons](const QItemSelection &, const QItemSelection &) {
            updateActionButtons();
        });

    m_ui->table->horizontalHeader()->hide();
    m_ui->table->horizontalHeader()->setSectionResizeMode(0,
                                                          QHeaderView::Stretch);
    m_ui->table->verticalHeader()->hide();
    m_ui->table->verticalHeader()->setDefaultSectionSize(30);
    m_ui->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ui->table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_ui->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ui->editButton->setEnabled(false);
    m_ui->copyButton->setEnabled(false);
    m_ui->deleteButton->setEnabled(false);
    refreshHistory();
    SPDLOG_DEBUG("HistoryWidget: constructor end");
}

HistoryWidget::~HistoryWidget() = default;

void HistoryWidget::setHistory(RecognitionHistory *history)
{
    m_history = history;
    m_model->setHistory(history);
}

void HistoryWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        m_ui->retranslateUi(this);
        refreshHistory();
    }
}

void HistoryWidget::refreshHistory()
{
    SPDLOG_DEBUG("refreshHistory: begin");
    if (!m_history) {
        SPDLOG_DEBUG("refreshHistory: no history backend");
        return;
    }

    m_model->reload();
    SPDLOG_DEBUG("refreshHistory: end");
}

int HistoryWidget::selectedRow() const
{
    const QModelIndex index = m_ui->table->currentIndex();
    return index.isValid() ? index.row() : -1;
}

void HistoryWidget::editEntry()
{
    const auto *entry = m_model->entryAt(selectedRow());
    if (!m_history || !entry) {
        return;
    }

    QDialog dialog(this);
    dialog.setMinimumSize(480, 260);

    auto m_ui = std::make_unique<Ui::HistoryEditDialog>();
    m_ui->setupUi(&dialog);
    m_ui->editor->setPlainText(entry->text);
    m_ui->editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_ui->editor->selectAll();

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString newText = m_ui->editor->toPlainText().trimmed();
    if (newText.isEmpty() || newText == entry->text) {
        return;
    }

    m_history->updateEntry(entry->id, newText);
    refreshHistory();
    STATUSBAR_INFO("{}", tr("Updated"));
}

void HistoryWidget::copyEntry()
{
    if (!m_history) {
        return;
    }

    QStringList texts;
    const QModelIndexList rows = m_ui->table->selectionModel()->selectedRows();
    for (const QModelIndex &index : rows) {
        if (const auto *entry = m_model->entryAt(index.row())) {
            texts.append(entry->text);
        }
    }
    if (texts.isEmpty()) {
        return;
    }

    QApplication::clipboard()->setText(texts.join(QLatin1Char('\n')));
    STATUSBAR_INFO("{}", tr("Copied"));
}

void HistoryWidget::deleteEntry()
{
    if (!m_history) {
        return;
    }

    const QModelIndexList rows = m_ui->table->selectionModel()->selectedRows();
    QVector<qint64> ids;
    for (const QModelIndex &index : rows) {
        if (const auto *entry = m_model->entryAt(index.row())) {
            ids.append(entry->id);
        }
    }
    if (ids.isEmpty()) {
        return;
    }

    for (const qint64 id : ids) {
        m_history->deleteEntry(id);
    }
    refreshHistory();
    STATUSBAR_INFO("{}", tr("Deleted"));
}

void HistoryWidget::clearHistory()
{
    if (!m_history) {
        return;
    }

    const auto reply = QMessageBox::question(
        this, tr("Clear History"),
        tr("Are you sure you want to clear all recognition history?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        return;
    }

    m_history->clearAll();
    refreshHistory();
    STATUSBAR_INFO("{}", tr("History cleared"));
}

} // namespace zenny
