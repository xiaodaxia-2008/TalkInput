#include "recognition_history.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include <spdlog/spdlog.h>

namespace talkinput {

RecognitionHistory::RecognitionHistory() {
  QString dbDir = QStandardPaths::writableLocation(
      QStandardPaths::AppDataLocation);
  if (dbDir.isEmpty())
    dbDir = QCoreApplication::applicationDirPath();
  QDir().mkpath(dbDir);

  const QString dbPath = QDir(dbDir).filePath(QStringLiteral("history.db"));

  m_db = new QSqlDatabase(
      QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                QStringLiteral("history")));
  m_db->setDatabaseName(dbPath);

  if (!m_db->open()) {
    spdlog::error("Failed to open history db: {}",
                  m_db->lastError().text().toStdString());
    return;
  }

  QSqlQuery q(*m_db);
  q.exec(QStringLiteral(
      "CREATE TABLE IF NOT EXISTS recognitions ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
      "  text TEXT NOT NULL,"
      "  created_at TEXT NOT NULL"
      ")"));
  spdlog::info("History db opened: {}", dbPath.toStdString());
}

RecognitionHistory::~RecognitionHistory() {
  if (m_db) {
    m_db->close();
    delete m_db;
  }
}

void RecognitionHistory::addEntry(const QString &text) {
  if (text.trimmed().isEmpty() || !m_db)
    return;

  QSqlQuery q(*m_db);
  q.prepare(QStringLiteral(
      "INSERT INTO recognitions (text, created_at) VALUES (?, ?)"));
  q.addBindValue(text.trimmed());
  q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
  if (!q.exec())
    spdlog::error("Failed to insert history: {}",
                  q.lastError().text().toStdString());
}

void RecognitionHistory::deleteEntry(int id) {
  if (!m_db)
    return;

  QSqlQuery q(*m_db);
  q.prepare(QStringLiteral("DELETE FROM recognitions WHERE id = ?"));
  q.addBindValue(id);
  if (!q.exec())
    spdlog::error("Failed to delete history entry {}: {}", id,
                  q.lastError().text().toStdString());
}

void RecognitionHistory::clearAll() {
  if (!m_db)
    return;

  QSqlQuery q(*m_db);
  if (!q.exec(QStringLiteral("DELETE FROM recognitions")))
    spdlog::error("Failed to clear history: {}",
                  q.lastError().text().toStdString());
}

QVector<RecognitionHistory::Entry> RecognitionHistory::allEntries() const {
  QVector<Entry> result;

  if (!m_db)
    return result;

  QSqlQuery q(*m_db);
  q.exec(QStringLiteral("SELECT id, text, created_at FROM recognitions "
                         "ORDER BY id DESC"));
  while (q.next()) {
    Entry e;
    e.id = q.value(0).toInt();
    e.text = q.value(1).toString();
    e.createdAt =
        QDateTime::fromString(q.value(2).toString(), Qt::ISODate);
    result.append(e);
  }

  return result;
}

} // namespace talkinput
