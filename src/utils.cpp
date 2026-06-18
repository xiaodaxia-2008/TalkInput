#include "utils.h"

#include <QAbstractButton>
#include <QIcon>
#include <QSize>

namespace talkinput
{

QIcon resourceIcon(const QString &iconPath)
{
    return QIcon(iconPath);
}

void setButtonIcon(QAbstractButton *button, const QString &iconPath, int size,
                   bool clearText)
{
    if (!button) {
        return;
    }

    button->setIcon(resourceIcon(iconPath));
    button->setIconSize(QSize(size, size));
    if (clearText) {
        button->setText({});
    }
}

} // namespace talkinput
