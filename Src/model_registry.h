#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace talkinput
{

struct ModelPreset
{
    QString name;
    QString typeStr;
    QString languages;
    QString modelDirName;
    QString url;
    qint64 size = 0;
    int paramCount = 0;
    bool streamingSupport = false;
    bool isPunctuationModel = false;

    struct FileRule
    {
        QString configField;
        QStringList globPatterns;
        bool isDir = false;
    };

    QVector<FileRule> files;
};

struct ModelFileSet
{
    QString typeStr;
    QString modelDirName;
    QMap<QString, QString> resolvedFiles;
    bool matched = false;
};

struct LlmLocalModel
{
    QString name;
    QString url;
    QString fileName;
    qint64 size = 0;
};

struct LlmProviderPreset
{
    QString id;
    QString name;
    QString endpoint;
    QString model;
    QStringList models;
    bool custom = false;
    bool managedLocalService = false;
};

QVector<ModelPreset> loadModelPresets();
QVector<ModelPreset> loadToolPresets();
LlmLocalModel loadLlmLocalModel();
QVector<LlmProviderPreset> loadLlmProviderPresets();
LlmProviderPreset defaultLlmProvider();
LlmProviderPreset findLlmProviderPreset(const QString &id);
QString defaultLlmProviderId();
QString defaultLlmEndpoint();
QString defaultLlmModel();
QString defaultLlmSystemPrompt();
ModelFileSet resolveModelFiles(const QString &modelDir);

} // namespace talkinput
