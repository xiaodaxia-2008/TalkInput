#include "archive_utils.h"

#include <archive.h>
#include <archive_entry.h>

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>

#include <memory>
#include <expected>

namespace
{

bool isPathInsideDir(const QString &path, const QString &dir)
{
    const QString absPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString absDir = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
    return absPath == absDir || absPath.startsWith(absDir + '/') ||
           absPath.startsWith(absDir + '\\');
}

bool isUnsafeArchivePath(const QString &path)
{
    return path.isEmpty() || path.startsWith('/') || path.startsWith('\\') ||
           path == ".." || path.startsWith("../") || path.startsWith("..\\") ||
           path.contains(':');
}

QString entryPath(struct archive_entry *entry)
{
    const char *utf8 = archive_entry_pathname_utf8(entry);
    return utf8 ? QString::fromUtf8(utf8)
                : QString::fromLocal8Bit(archive_entry_pathname(entry));
}

} // namespace

namespace talkinput
{

std::expected<void, QString> extractArchive(const QString &archivePath,
                                            const QString &destDir)
{
    QDir dest(destDir);
    if (!dest.exists() && !dest.mkpath(".")) {
        return std::unexpected(QString("Cannot create: %1").arg(destDir));
    }

    std::unique_ptr<archive, decltype(&archive_read_free)> reader(
        archive_read_new(), archive_read_free);
    archive_read_support_filter_all(reader.get());
    archive_read_support_format_all(reader.get());

    if (archive_read_open_filename(
            reader.get(),
            QDir::toNativeSeparators(archivePath).toLocal8Bit().constData(),
            10240) != ARCHIVE_OK)
    {
        return std::unexpected(
            QString::fromLocal8Bit(archive_error_string(reader.get())));
    }

    archive_entry *entry = nullptr;
    while (archive_read_next_header(reader.get(), &entry) == ARCHIVE_OK) {
        const QString rel = QDir::cleanPath(entryPath(entry));
        if (isUnsafeArchivePath(rel)) {
            return std::unexpected(QString("Unsafe path: %1").arg(rel));
        }

        const QString outPath = dest.filePath(rel);
        if (!isPathInsideDir(outPath, dest.absolutePath())) {
            return std::unexpected(QString("Escapes destination: %1").arg(rel));
        }

        const auto fileType = archive_entry_filetype(entry);
        if (fileType == AE_IFDIR) {
            QDir().mkpath(outPath);
            archive_read_data_skip(reader.get());
            continue;
        }
        if (fileType != AE_IFREG) {
            archive_read_data_skip(reader.get());
            continue;
        }

        QDir().mkpath(QFileInfo(outPath).absolutePath());
        QFile outFile(outPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return std::unexpected(QString("Cannot write: %1").arg(outPath));
        }

        const void *buffer = nullptr;
        size_t size = 0;
        la_int64_t offset = 0;
        while (archive_read_data_block(reader.get(), &buffer, &size, &offset) ==
               ARCHIVE_OK)
        {
            Q_UNUSED(offset);
            if (outFile.write(static_cast<const char *>(buffer),
                              static_cast<qint64>(size)) !=
                static_cast<qint64>(size))
            {
                return std::unexpected(
                    QString("Write error: %1").arg(outPath));
            }
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    return {};
}

} // namespace talkinput
