#include "utils.h"

#include <QAbstractButton>
#include <QIcon>
#include <QSize>

namespace talkinput
{

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
