#pragma once

#include <QIcon>
#include <QString>

class QAbstractButton;

namespace talkinput
{

QIcon resourceIcon(const QString &iconPath);

void setButtonIcon(QAbstractButton *button, const QString &iconPath, int size,
                   bool clearText = true);

} // namespace talkinput
