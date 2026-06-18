#pragma once

#include <QString>

class QAbstractButton;

namespace talkinput
{

QString appDataDir();
void setButtonIcon(QAbstractButton *button, const QString &iconPath, int size,
                   bool clearText = true);

} // namespace talkinput
