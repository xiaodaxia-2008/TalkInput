#pragma once

#include <QString>

class QObject;
class QTranslator;

namespace talkinput
{

QString systemAppLanguage();
QString currentAppLanguage();
void installAppTranslations(const QString &language, QObject *parent,
                            QTranslator *&appTranslator,
                            QTranslator *&qtTranslator);

} // namespace talkinput
