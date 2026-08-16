#include "melo_tts_engine.h"

#include "../app_config.h"
#include "../logging.h"
#include "../utils.h"
#include "tts_audio.h"

#include <sherpa-onnx/c-api/c-api.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <cstring>

namespace talkinput
{

namespace
{

constexpr int kMeloNumThreads = 4;

QString resolveModelDir(const QString &dirName)
{
    if (dirName.isEmpty()) {
        return {};
    }
    const QString exeDir =
        QDir(QCoreApplication::applicationDirPath())
            .filePath(QStringLiteral("models/%1").arg(dirName));
    if (QFileInfo(exeDir).isDir()) {
        return exeDir;
    }
    return QDir(appDataDir()).filePath(QStringLiteral("models/%1").arg(dirName));
}

QStringList defaultFstPaths(const QString &modelDir)
{
    QStringList fsts;
    for (const char *name :
         {"date.fst", "number.fst", "phone.fst", "new_heteronym.fst"})
    {
        const QString path = QDir(modelDir).filePath(QLatin1String(name));
        if (QFileInfo::exists(path)) {
            fsts.append(path);
        }
    }
    return fsts;
}

} // namespace

MeloTtsEngine::MeloTtsEngine() = default;

MeloTtsEngine::~MeloTtsEngine()
{
    if (m_tts) {
        SherpaOnnxDestroyOfflineTts(
            static_cast<const SherpaOnnxOfflineTts *>(m_tts));
    }
}

QString MeloTtsEngine::modelDir() const
{
    return resolveModelDir(
        QString::fromStdString(appConfig().settings.ttsMeloModelDirName));
}

bool MeloTtsEngine::isModelInstalled()
{
    const QString dir = resolveModelDir(
        QString::fromStdString(appConfig().settings.ttsMeloModelDirName));
    if (dir.isEmpty()) {
        return false;
    }
    return QFileInfo(QDir(dir).filePath(QStringLiteral("model.onnx")))
               .isFile() &&
           QFileInfo(QDir(dir).filePath(QStringLiteral("tokens.txt"))).isFile();
}

bool MeloTtsEngine::ensureLoaded(QString *error)
{
    if (m_tts) {
        return true;
    }

    const QString dir = modelDir();
    const QString modelPath = QDir(dir).filePath(QStringLiteral("model.onnx"));
    const QString tokensPath = QDir(dir).filePath(QStringLiteral("tokens.txt"));
    const QString lexiconPath = QDir(dir).filePath(QStringLiteral("lexicon.txt"));
    if (!QFileInfo(modelPath).isFile() || !QFileInfo(tokensPath).isFile()) {
        *error = QStringLiteral("MeloTTS model not installed at %1")
                     .arg(QDir::toNativeSeparators(dir));
        return false;
    }

    const QByteArray modelUtf8 = modelPath.toUtf8();
    const QByteArray tokensUtf8 = tokensPath.toUtf8();
    const QByteArray lexiconUtf8 = lexiconPath.toUtf8();
    QList<QByteArray> fstData;
    for (const QString &fst : defaultFstPaths(dir)) {
        fstData.append(fst.toUtf8());
    }
    const QByteArray ruleFsts = fstData.join(',');

    SherpaOnnxOfflineTtsConfig config;
    std::memset(&config, 0, sizeof(config));
    config.model.vits.model = modelUtf8.constData();
    config.model.vits.tokens = tokensUtf8.constData();
    config.model.vits.lexicon = lexiconUtf8.constData();
    config.model.num_threads = kMeloNumThreads;
    config.model.provider = "cpu";
    config.rule_fsts = ruleFsts.isEmpty() ? nullptr : ruleFsts.constData();
    config.max_num_sentences = 1;
    config.silence_scale = 0.2f;

    const SherpaOnnxOfflineTts *tts = SherpaOnnxCreateOfflineTts(&config);
    if (!tts) {
        *error = QStringLiteral("Failed to initialize MeloTTS model.");
        return false;
    }
    m_tts = tts;
    m_sampleRate = SherpaOnnxOfflineTtsSampleRate(tts);
    SPDLOG_INFO("MeloTTS: model loaded ({} Hz, {} speaker(s))", m_sampleRate,
                SherpaOnnxOfflineTtsNumSpeakers(tts));
    return true;
}

TtsSynthesisResult MeloTtsEngine::synthesize(const QString &text,
                                             const QString &voice,
                                             double speed)
{
    Q_UNUSED(voice);
    TtsSynthesisResult result;

    QString error;
    if (!ensureLoaded(&error)) {
        result.error = error;
        return result;
    }

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        result.error = QStringLiteral("Cannot synthesize empty text.");
        return result;
    }

    const QByteArray textUtf8 = trimmed.toUtf8();

    SherpaOnnxGenerationConfig generation;
    std::memset(&generation, 0, sizeof(generation));
    generation.sid = 0;
    generation.speed = static_cast<float>(std::clamp(speed, 0.25, 4.0));
    generation.silence_scale = 0.2f;

    const SherpaOnnxGeneratedAudio *audio =
        SherpaOnnxOfflineTtsGenerateWithConfig(
            static_cast<const SherpaOnnxOfflineTts *>(m_tts),
            textUtf8.constData(), &generation, nullptr, nullptr);
    if (!audio || audio->n <= 0 || audio->sample_rate <= 0) {
        result.error = QStringLiteral("MeloTTS synthesis failed.");
        if (audio) {
            SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        }
        return result;
    }

    result.pcm24k = resampleFloatToInt16(audio->samples, audio->n,
                                         audio->sample_rate, 24000);
    const double durationSec =
        static_cast<double>(audio->n) / audio->sample_rate;
    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);

    if (result.pcm24k.isEmpty()) {
        result.error = QStringLiteral("MeloTTS produced no audio.");
        return result;
    }

    SPDLOG_INFO("MeloTTS: synthesized {} chars -> {} samples ({:.2f}s)",
                text.size(), result.pcm24k.size() / 2, durationSec);
    return result;
}

} // namespace talkinput
