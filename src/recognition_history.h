#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

#include <memory>

class QSqlDatabase;

namespace talkinput
{

class RecognitionHistory
{
public:
    struct Entry
    {
        int id = 0;
        QString text;
        QDateTime createdAt;
    };

    RecognitionHistory();
    ~RecognitionHistory();

    void addEntry(const QString &text);
    void updateEntry(int id, const QString &text);
    void deleteEntry(int id);
    void clearAll();
    QVector<Entry> allEntries() const;

private:
    std::unique_ptr<QSqlDatabase> m_db;
};

} // namespace talkinput
