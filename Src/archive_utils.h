#pragma once

#include <QString>

namespace talkinput
{

bool extractArchive(const QString &archivePath, const QString &destDir,
                    QString *errorMessage = nullptr);

} // namespace talkinput
