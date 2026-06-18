#include "history_widget.h"
#include "logging.h"
#include "recognition_history.h"
#include "utils.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
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
    : QWidget(parent), m_history(history)
{
    SPDLOG_DEBUG("HistoryWidget: constructor begin");
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);
    SPDLOG_DEBUG("HistoryWidget: root layout created");

    m_realtimeLabel = new QLabel(this);
    m_realtimeLabel->setObjectName("historyRealtimeLabel");
    m_realtimeLabel->setWordWrap(true);
    m_realtimeLabel->setTextFormat(Qt::PlainText);
    m_realtimeLabel->setMinimumHeight(36);
    m_realtimeLabel->hide();
    root->addWidget(m_realtimeLabel);
    SPDLOG_DEBUG("HistoryWidget: realtime label created");

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 4, 0, 0);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName("historyTitleLabel");
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();

    m_clearButton = new QPushButton(this);
    m_clearButton->setObjectName("historyClearButton");
    m_clearButton->setFlat(true);
    connect(m_clearButton, &QPushButton::clicked, this,
            &HistoryWidget::clearHistory);
    headerLayout->addWidget(m_clearButton);

    root->addLayout(headerLayout);
    SPDLOG_DEBUG("HistoryWidget: header created");

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->horizontalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setColumnWidth(1, 32);
    m_table->setColumnWidth(2, 32);
    m_table->setColumnWidth(3, 32);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setDefaultSectionSize(30);
    root->addWidget(m_table);
    SPDLOG_DEBUG("HistoryWidget: table created");

    retranslateUi();
    SPDLOG_DEBUG("HistoryWidget: constructor end");
}

void HistoryWidget::refreshHistory()
{
    SPDLOG_DEBUG("HistoryWidget::refreshHistory: begin");
    if (!m_history) {
        SPDLOG_DEBUG("HistoryWidget::refreshHistory: no history backend");
        return;
    }

    const auto entries = m_history->allEntries();
    SPDLOG_DEBUG("HistoryWidget::refreshHistory: {} entries", entries.size());

    m_table->setUpdatesEnabled(false);
    m_table->setRowCount(entries.size());

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
        m_table->setItem(i, 0, textItem);

        auto *editButton = new QPushButton();
        setButtonIcon(editButton, ":/resources/edit.svg", 18);
        editButton->setToolTip(tr("Edit text"));
        editButton->setFlat(true);
        connect(editButton, &QPushButton::clicked, this,
                [this, i]() { editEntry(i); });

        auto *copyButton = new QPushButton();
        setButtonIcon(copyButton, ":/resources/copy.svg", 18);
        copyButton->setToolTip(tr("Copy text"));
        copyButton->setFlat(true);
        connect(copyButton, &QPushButton::clicked, this,
                [this, i]() { copyEntry(i); });

        auto *deleteButton = new QPushButton();
        setButtonIcon(deleteButton, ":/resources/delete.svg", 18);
        deleteButton->setToolTip(tr("Delete entry"));
        deleteButton->setFlat(true);
        connect(deleteButton, &QPushButton::clicked, this,
                [this, i]() { deleteEntry(i); });

        m_table->setCellWidget(i, 1, editButton);
        m_table->setCellWidget(i, 2, copyButton);
        m_table->setCellWidget(i, 3, deleteButton);
    }

    m_table->setUpdatesEnabled(true);
    SPDLOG_DEBUG("HistoryWidget::refreshHistory: end");
}

void HistoryWidget::setListening(bool listening)
{
    if (listening) {
        m_realtimeLabel->clear();
        m_realtimeLabel->show();
        return;
    }

    m_realtimeLabel->hide();
}

void HistoryWidget::setRealtimeText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    m_realtimeLabel->setText(trimmed);
    m_realtimeLabel->show();
}

void HistoryWidget::retranslateUi()
{
    m_titleLabel->setText(tr("Recognition History"));
    m_clearButton->setText(tr("Clear"));
    refreshHistory();
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
    emit statusMessage(tr("Updated."));
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
    emit statusMessage(tr("Copied."));
}

void HistoryWidget::deleteEntry(int row)
{
    auto *item = m_table->item(row, 0);
    if (!m_history || !item) {
        return;
    }

    const int id = item->data(Qt::UserRole).toInt();
    m_history->deleteEntry(id);
    refreshHistory();
    emit statusMessage(tr("Deleted."));
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
    emit statusMessage(tr("History cleared."));
}

} // namespace talkinput
