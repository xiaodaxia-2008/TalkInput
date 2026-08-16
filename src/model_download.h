#pragma once

#include "app_config.h"

#include <QCoro/QCoroTask>

#include <QString>

namespace talkinput
{

/// Human-readable label for an ASR preset, e.g.
/// "SenseVoice - Offline - Multilingual".
QString asrModelLabel(const AsrPreset &model);

/// Checks whether the model files are present either next to the executable
/// (bundled with an installation) or in the user data directory (downloaded).
bool isModelInstalled(const std::string &modelDirName,
                      const std::map<std::string, std::string> &files);

struct ModelArchiveResult
{
    bool ok = false;
    QString error;
};

/// Downloads @p url into the user model cache, extracts the archive with
/// libarchive, and deletes the archive. Shared by the ASR and TTS model
/// download flows. @p serviceName is used in status bar messages, e.g.
/// "ASR" or "TTS".
QCoro::Task<ModelArchiveResult>
downloadModelArchive(const QString &modelLabel, const QString &url,
                     const QString &serviceName);

} // namespace talkinput
