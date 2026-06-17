#include "model_registry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace talkinput
{

static QVector<ModelPreset> s_presets;
static bool s_loaded = false;

static void ensureLoaded()
{
    if (s_loaded) {
        return;
    }
    s_loaded = true;

    QFile f(QStringLiteral(":/resources/models.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "model_registry: cannot open models.json resource";
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "model_registry: JSON parse error:" << err.errorString();
        return;
    }

    const QJsonArray arr =
        doc.object().value(QStringLiteral("presets")).toArray();
    for (const auto &val : arr) {
        const QJsonObject obj = val.toObject();
        ModelPreset preset;
        preset.name = obj.value(QStringLiteral("name")).toString();
        preset.typeStr = obj.value(QStringLiteral("type")).toString();
        preset.modelDirName =
            obj.value(QStringLiteral("modelDirName")).toString();
        preset.url = obj.value(QStringLiteral("url")).toString();
        preset.size =
            static_cast<qint64>(obj.value(QStringLiteral("size")).toDouble());
        preset.paramCount = obj.value(QStringLiteral("paramCount")).toInt();
        preset.streamingSupport =
            obj.value(QStringLiteral("streamingSupport")).toBool();

        const QJsonObject filesObj =
            obj.value(QStringLiteral("files")).toObject();
        for (auto it = filesObj.begin(); it != filesObj.end(); ++it) {
            const QString key = it.key();

            ModelPreset::FileRule rule;
            if (key.endsWith(QStringLiteral(">dir"))) {
                rule.configField = key.chopped(5);
                rule.isDir = true;
            }
            else {
                rule.configField = key;
                rule.isDir = false;
            }
            rule.globPatterns.append(it.value().toString());
            preset.files.append(rule);
        }

        s_presets.append(preset);
    }

    qInfo() << "model_registry: loaded" << s_presets.size() << "presets";
}

QVector<ModelPreset> loadModelPresets()
{
    ensureLoaded();
    return s_presets;
}

static QStringList findFiles(const QDir &dir, const QStringList &names,
                             bool isDir)
{
    for (const QString &name : names) {
        const QString full = dir.absoluteFilePath(name);
        if (isDir) {
            if (QFileInfo(full).isDir()) {
                return {full};
            }
        }
        else {
            if (QFileInfo::exists(full) && QFileInfo(full).isFile()) {
                return {full};
            }
        }
    }
    return {};
}

ModelFileSet resolveModelFiles(const QString &modelDir)
{
    ensureLoaded();

    const QDir dir(modelDir);
    const QString dirName = dir.dirName();

    // Find matching preset
    const ModelPreset *preset = nullptr;
    for (const auto &p : s_presets) {
        if (p.modelDirName == dirName) {
            preset = &p;
            break;
        }
    }

    ModelFileSet result;
    if (!preset) {
        qDebug() << "model_registry: no preset for" << dirName
                 << "caller should fall back to probing";
        result.modelDirName = dirName;
        return result;
    }

    result.typeStr = preset->typeStr;
    result.modelDirName = preset->modelDirName;
    result.matched = true;

    for (const auto &rule : preset->files) {
        const QStringList found = findFiles(dir, rule.globPatterns, rule.isDir);
        if (found.isEmpty()) {
            qWarning() << "model_registry: no match for" << rule.configField
                       << "in" << dirName;
            continue;
        }
        // Use the first match (preferring int8)
        result.resolvedFiles.insert(rule.configField, found.first());
    }

    return result;
}

} // namespace talkinput
