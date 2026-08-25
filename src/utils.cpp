#include "utils.h"

#include <QDir>
#include <QStandardPaths>

namespace zenny
{
QString appDataDir()
{
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        return QDir::current().filePath(".ZennyData");
    }
    return base;
}

} // namespace zenny
