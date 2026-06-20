#pragma once

#include <QObject>
#include <QString>

namespace talkinput
{

class TextInjector final : public QObject
{
public:
    explicit TextInjector(QObject *parent = nullptr);

    bool inject(const QString &text);
};

} // namespace talkinput
