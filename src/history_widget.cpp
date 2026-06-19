#include "history_widget.h"
#include "logging.h"
#include "recognition_history.h"
#include "ui_history_widget.h"
#include "utils.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextEdit>
#include <QTextOption>
#include <QVBoxLayout>

namespace talkinput
{

HistoryWidget::HistoryWidget(RecognitionHistory *history, QWidget *parent)
    : QWidget(parent), m_ui(std::make_unique<Ui::HistoryWidget>()),
      m_history(history)
{
    SPDLOG_DEBUG("HistoryWidget: constructor begin");
    m_ui->setupUi(this);
    connect(m_ui->clearButton, &QPushButton::clicked, this,
            &HistoryWidget::clearHistory);

    m_ui->table->horizontalHeader()->hide();
    m_ui->table->horizontalHeader()->setSectionResizeMode(0,
                                                          QHeaderView::Stretch);
    m_ui->table->setColumnWidth(1, 32);
    m_ui->table->setColumnWidth(2, 32);
    m_ui->table->setColumnWidth(3, 32);
    m_ui->table->verticalHeader()->hide();
    m_ui->table->verticalHeader()->setDefaultSectionSize(30);
    refreshHistory();
    SPDLOG_DEBUG("HistoryWidget: constructor end");
}

HistoryWidget::~HistoryWidget() = default;

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

    const auto entries = m_history->allEntries();
    SPDLOG_DEBUG("refreshHistory: {} entries", entries.size());

    m_ui->table->setUpdatesEnabled(false);
    m_ui->table->setRowCount(entries.size());

    for (int i = 0; i < entries.size(); ++i) {
        const auto &entry = entries.at(i);

        QString display = entry.text;
        if (display.length() > 55) {
            display = display.left(55) + "...";
        }

        auto *textItem = new QTableWidgetItem(display);
        textItem->setData(Qt::UserRole, entry.id);
        textItem->setToolTip(entry.text);
        textItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_ui->table->setItem(i, 0, textItem);

        auto *editButton = new QPushButton();
        setButtonIcon(editButton, ":/resources/icons/edit.svg", 18);
        editButton->setToolTip(tr("Edit text"));
        editButton->setFlat(true);
        connect(editButton, &QPushButton::clicked, this,
                [this, i]() { editEntry(i); });

        auto *copyButton = new QPushButton();
        setButtonIcon(copyButton, ":/resources/icons/copy.svg", 18);
        copyButton->setToolTip(tr("Copy text"));
        copyButton->setFlat(true);
        connect(copyButton, &QPushButton::clicked, this,
                [this, i]() { copyEntry(i); });

        auto *deleteButton = new QPushButton();
        setButtonIcon(deleteButton, ":/resources/icons/delete.svg", 18);
        deleteButton->setToolTip(tr("Delete entry"));
        deleteButton->setFlat(true);
        connect(deleteButton, &QPushButton::clicked, this,
                [this, i]() { deleteEntry(i); });

        m_ui->table->setCellWidget(i, 1, editButton);
        m_ui->table->setCellWidget(i, 2, copyButton);
        m_ui->table->setCellWidget(i, 3, deleteButton);
    }

    m_ui->table->setUpdatesEnabled(true);
    SPDLOG_DEBUG("refreshHistory: end");
}

void HistoryWidget::editEntry(int row)
{
    if (!m_history || row < 0) {
        return;
    }

    const auto entries = m_history->allEntries();
    if (row >= entries.size()) {
        return;
    }

    const auto &entry = entries.at(row);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Edit Recognition Text"));
    dialog.setMinimumSize(480, 260);

    auto *layout = new QVBoxLayout(&dialog);

    auto *editor = new QTextEdit(&dialog);
    editor->setPlainText(entry.text);
    editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    editor->selectAll();
    layout->addWidget(editor);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString newText = editor->toPlainText().trimmed();
    if (newText.isEmpty() || newText == entry.text) {
        return;
    }

    m_history->updateEntry(entry.id, newText);
    refreshHistory();
    spdlog::get("statusbar")->info("{}", tr("Updated"));
}

void HistoryWidget::copyEntry(int row)
{
    if (!m_history || row < 0) {
        return;
    }

    const auto entries = m_history->allEntries();
    if (row >= entries.size()) {
        return;
    }

    QApplication::clipboard()->setText(entries.at(row).text);
    spdlog::get("statusbar")->info("{}", tr("Copied"));
}

void HistoryWidget::deleteEntry(int row)
{
    auto *item = m_ui->table->item(row, 0);
    if (!m_history || !item) {
        return;
    }

    const int id = item->data(Qt::UserRole).toInt();
    m_history->deleteEntry(id);
    refreshHistory();
    spdlog::get("statusbar")->info("{}", tr("Deleted"));
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
    spdlog::get("statusbar")->info("{}", tr("History cleared"));
}

} // namespace talkinput
