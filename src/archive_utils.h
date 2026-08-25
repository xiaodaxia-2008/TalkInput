#pragma once

#include <QString>
#include <expected>

namespace zenny
{

std::expected<void, QString> extractArchive(const QString &archivePath,
                                            const QString &destDir);

} // namespace zenny
