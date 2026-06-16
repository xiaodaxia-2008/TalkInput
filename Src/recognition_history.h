#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

class QSqlDatabase;

namespace talkinput {

class RecognitionHistory {
public:
  struct Entry {
    int id = 0;
    QString text;
    QDateTime createdAt;
  };

  RecognitionHistory();
  ~RecognitionHistory();

  void addEntry(const QString &text);
  void deleteEntry(int id);
  void clearAll();
  QVector<Entry> allEntries() const;

private:
  QSqlDatabase *m_db = nullptr;
};

} // namespace talkinput
