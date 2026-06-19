#include "rapid_ocr_recognizer.h"
#include "logging.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QTemporaryFile>

namespace talkinput
{

bool RapidOcrRecognizer::isAvailable() const
{
    return QFileInfo::exists(
        QCoreApplication::applicationDirPath() +
        QStringLiteral("/scripts/rapidocr.py"));
}

void RapidOcrRecognizer::recognizeText(const QImage &image,
                                       QObject *receiver,
                                       Callback callback)
{
    if (!receiver || !callback || image.isNull()) {
        return;
    }

    auto *tempFile = new QTemporaryFile(this);
    tempFile->setAutoRemove(true);
    if (!tempFile->open()) {
        SPDLOG_WARN("RapidOcr: failed to create temp file");
        QMetaObject::invokeMethod(
            receiver, [callback = std::move(callback)]() { callback({}); },
            Qt::QueuedConnection);
        return;
    }

    const QString tempPath = QFileInfo(*tempFile).absoluteFilePath();
    if (!image.save(tempPath, "PNG")) {
        SPDLOG_WARN("RapidOcr: failed to save image to temp file");
        QMetaObject::invokeMethod(
            receiver, [callback = std::move(callback)]() { callback({}); },
            Qt::QueuedConnection);
        return;
    }
    tempFile->close();

    const QString scriptPath =
        QCoreApplication::applicationDirPath() +
        QStringLiteral("/scripts/rapidocr.py");

    const QString scriptsDir =
        QCoreApplication::applicationDirPath() +
        QStringLiteral("/scripts");

    auto *process = new QProcess(this);
    process->setWorkingDirectory(scriptsDir);
    process->setProgram("uv");
    process->setArguments({"run", scriptPath, tempPath});

    QPointer<QObject> receiverPtr(receiver);
    auto *tempFilePtr = tempFile;
    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(
                &QProcess::finished),
            this,
            [process, tempFilePtr, receiverPtr,
             callback = std::move(callback)](
                int exitCode, QProcess::ExitStatus) mutable {
                QString text;
                if (exitCode == 0) {
                    text = QString::fromUtf8(process->readAllStandardOutput())
                               .trimmed();
                }
                else {
                    const QString err =
                        QString::fromUtf8(
                            process->readAllStandardError())
                            .trimmed();
                    SPDLOG_WARN("RapidOcr: process failed: {}", err);
                }

                delete tempFilePtr;
                process->deleteLater();

                if (receiverPtr) {
                    QMetaObject::invokeMethod(
                        receiverPtr.data(),
                        [callback = std::move(callback),
                         text]() mutable { callback(text); },
                        Qt::QueuedConnection);
                }
            });

    SPDLOG_DEBUG("RapidOcr: starting uv run {} {}",
                 scriptPath, tempPath);
    process->start();
}

} // namespace talkinput
