#include "utils.h"

#include <QAbstractButton>
#include <QDir>
#include <QIcon>
#include <QSize>
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

void setButtonIcon(QAbstractButton *button, const QString &iconPath, int size,
                   bool clearText)
{
    if (!button) {
        return;
    }

    button->setIcon(QIcon(iconPath));
    button->setIconSize(QSize(size, size));
    if (clearText) {
        button->setText({});
    }
}


} // namespace talkinput
