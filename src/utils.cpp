#include "utils.h"

#include <QDir>
#include <QStandardPaths>

namespace talkinput
{
QString appDataDir()
{
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        return QDir::current().filePath(".TalkInputData");
    }
    return base;
}

} // namespace talkinput
