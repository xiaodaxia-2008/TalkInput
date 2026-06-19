#pragma once

#include <QString>
#include <expected>

namespace talkinput
{

std::expected<void, QString> extractArchive(const QString &archivePath,
                                            const QString &destDir);

} // namespace talkinput
