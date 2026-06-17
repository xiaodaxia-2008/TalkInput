#include "asr_service.h"
#include "model_registry.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>

#include <spdlog/spdlog.h>
#include "qt_fmt.h"

namespace {

QString findModelFile(const QDir &dir, const QStringList &patterns) {
  const auto files = dir.entryInfoList(patterns, QDir::Files, QDir::Name);
  if (files.isEmpty())
    return {};
  for (const QFileInfo &fi : files)
    if (fi.fileName().contains(QStringLiteral("int8")))
      return fi.absoluteFilePath();
  return files.first().absoluteFilePath();
}

QString findModelFileOrDir(const QDir &dir, const QStringList &patterns) {
  const auto files = dir.entryInfoList(patterns, QDir::Files | QDir::Dirs, QDir::Name);
  if (files.isEmpty())
    return {};
  for (const QFileInfo &fi : files)
    if (fi.fileName().contains(QStringLiteral("int8")))
      return fi.absoluteFilePath();
  return files.first().absoluteFilePath();
}

bool hasFile(const QDir &dir, const QStringList &patterns) {
  return !dir.entryInfoList(patterns, QDir::Files, QDir::Name).isEmpty();
}

talkinput::SpeechRecognizer::Type detectModelArch(const QString &modelDir) {
  const QDir dir(modelDir);

  const bool hasJoiner = hasFile(dir, {QStringLiteral("*joiner*")});
  const bool hasEncoder = hasFile(dir, {QStringLiteral("*encoder*")});
  const bool hasDecoder = hasFile(dir, {QStringLiteral("*decoder*")});
  const bool hasTokens = QFileInfo(dir.filePath(QStringLiteral("tokens.txt"))).exists();

  const bool hasFunasrFiles =
      hasFile(dir, {QStringLiteral("*encoder_adaptor*")}) &&
      hasFile(dir, {QStringLiteral("*llm*")}) &&
      hasFile(dir, {QStringLiteral("*embedding*")});

  const bool hasQwenFrontend = hasFile(dir, {QStringLiteral("*conv*frontend*")});
  const bool hasQwenTok = QFileInfo(dir.filePath(QStringLiteral("tokenizer"))).isDir();
  const bool hasModelInt8 = hasFile(dir, {QStringLiteral("model.int8.onnx")});

  if (hasJoiner && hasEncoder && hasDecoder && hasTokens)
    return talkinput::SpeechRecognizer::Type::StreamingTransducer;

  if (hasFunasrFiles)
    return talkinput::SpeechRecognizer::Type::FunASRNano;

  if (hasQwenFrontend && hasEncoder && hasDecoder && hasQwenTok)
    return talkinput::SpeechRecognizer::Type::Qwen3ASR;

  if (hasEncoder && hasDecoder && hasTokens)
    return talkinput::SpeechRecognizer::Type::StreamingParaformer;

  if (hasModelInt8 && hasTokens)
    return talkinput::SpeechRecognizer::Type::SenseVoice;

  spdlog::warn("AsrService: unknown model arch in {}, falling back to streaming transducer",
               modelDir);
  return talkinput::SpeechRecognizer::Type::StreamingTransducer;
}

talkinput::SpeechRecognizer::Type typeFromString(const QString &str) {
  if (str == QStringLiteral("SenseVoice"))
    return talkinput::SpeechRecognizer::Type::SenseVoice;
  if (str == QStringLiteral("FunASRNano"))
    return talkinput::SpeechRecognizer::Type::FunASRNano;
  if (str == QStringLiteral("Qwen3ASR"))
    return talkinput::SpeechRecognizer::Type::Qwen3ASR;
  if (str == QStringLiteral("StreamingParaformer"))
    return talkinput::SpeechRecognizer::Type::StreamingParaformer;
  return talkinput::SpeechRecognizer::Type::StreamingTransducer;
}

} // namespace

namespace talkinput {

AsrService::AsrService(QObject *parent)
    : QObject(parent) {
  m_recognizer = new SpeechRecognizer();
  connect(m_recognizer, &SpeechRecognizer::resultChanged,
          this, &AsrService::resultChanged);
}

AsrService::~AsrService() {
  unloadModel();
  delete m_recognizer;
}

void AsrService::setModelDirectory(const QString &dir) {
  m_modelDir = QDir::fromNativeSeparators(dir.trimmed());
  if (!m_modelDir.isEmpty())
    m_modelDir = QDir::cleanPath(m_modelDir);
}

SpeechRecognizer::Config AsrService::detectAndConfigure(const QString &modelDir) {
  const QDir dir(modelDir);
  SpeechRecognizer::Config config;
  config.modelDir = modelDir;

  // Try the preset registry first
  ModelFileSet resolved = resolveModelFiles(modelDir);
  if (resolved.matched) {
    config.type = typeFromString(resolved.typeStr);
    auto rel = [&](const QString &abs) { return dir.relativeFilePath(abs); };

    auto assign = [&](const char *key, QString &field) {
      const QString v = resolved.resolvedFiles.value(QString::fromLatin1(key));
      if (!v.isEmpty()) field = rel(v);
    };
    assign("encoderFile", config.encoderFile);
    assign("decoderFile", config.decoderFile);
    assign("joinerFile", config.joinerFile);
    assign("tokensFile", config.tokensFile);
    assign("senseVoiceModelFile", config.senseVoiceModelFile);
    assign("funasrEncoderAdaptorFile", config.funasrEncoderAdaptorFile);
    assign("funasrLlmFile", config.funasrLlmFile);
    assign("funasrEmbeddingFile", config.funasrEmbeddingFile);
    assign("qwen3ConvFrontendFile", config.qwen3ConvFrontendFile);
    assign("qwen3EncoderFile", config.qwen3EncoderFile);
    assign("qwen3DecoderFile", config.qwen3DecoderFile);

    // Directory fields (not relative to modelDir — already absolute or relative)
    {
      const QString tok = resolved.resolvedFiles.value(
          QStringLiteral("funasrTokenizerFile"));
      if (!tok.isEmpty())
        config.funasrTokenizerFile = dir.relativeFilePath(tok);
      else
        config.funasrTokenizerFile = QStringLiteral("Qwen3-0.6B");
    }
    {
      const QString tok = resolved.resolvedFiles.value(
          QStringLiteral("qwen3TokenizerDir"));
      if (!tok.isEmpty())
        config.qwen3TokenizerDir = tok; // absolute
    }

    // Non-file model config
    {
      QSettings s;
      config.senseVoiceLanguage =
          s.value(QStringLiteral("app/language"),
                  QStringLiteral("zh")).toString();
    }
    config.senseVoiceUseItn = true;

    spdlog::info("AsrService: configured from preset '{}'", resolved.typeStr);
  } else {
    // Fall back to file-probing detection
    const auto arch = detectModelArch(modelDir);
    config.type = arch;

    switch (arch) {
    case SpeechRecognizer::Type::StreamingTransducer: {
      auto rel = [&](const QString &abs) { return dir.relativeFilePath(abs); };
      config.encoderFile = rel(findModelFile(dir, {QStringLiteral("*encoder*")}));
      config.decoderFile = rel(findModelFile(dir, {QStringLiteral("*decoder*")}));
      config.joinerFile = rel(findModelFile(dir, {QStringLiteral("*joiner*")}));
      break;
    }
    case SpeechRecognizer::Type::StreamingParaformer: {
      auto rel = [&](const QString &abs) { return dir.relativeFilePath(abs); };
      config.encoderFile = rel(findModelFile(dir, {QStringLiteral("*encoder*")}));
      config.decoderFile = rel(findModelFile(dir, {QStringLiteral("*decoder*")}));
      break;
    }
    case SpeechRecognizer::Type::SenseVoice: {
      auto rel = [&](const QString &abs) { return dir.relativeFilePath(abs); };
      config.senseVoiceModelFile = rel(findModelFile(dir, {QStringLiteral("model.int8.onnx")}));
      {
        QSettings s;
        config.senseVoiceLanguage =
            s.value(QStringLiteral("app/language"),
                    QStringLiteral("zh")).toString();
      }
      config.senseVoiceUseItn = true;
      break;
    }
    case SpeechRecognizer::Type::FunASRNano: {
      auto rel = [&](const QString &abs) { return dir.relativeFilePath(abs); };
      config.funasrEncoderAdaptorFile = rel(findModelFile(dir, {QStringLiteral("*encoder_adaptor*")}));
      config.funasrLlmFile = rel(findModelFile(dir, {QStringLiteral("*llm*")}));
      config.funasrEmbeddingFile = rel(findModelFile(dir, {QStringLiteral("*embedding*")}));
      const auto tokDir = findModelFileOrDir(dir, {QStringLiteral("*Qwen3*"), QStringLiteral("*tokenizer*")});
      if (!tokDir.isEmpty())
        config.funasrTokenizerFile = dir.relativeFilePath(tokDir);
      else
        config.funasrTokenizerFile = QStringLiteral("Qwen3-0.6B");
      break;
    }
    case SpeechRecognizer::Type::Qwen3ASR: {
      auto rel = [&](const QString &abs) { return dir.relativeFilePath(abs); };
      config.qwen3ConvFrontendFile = rel(findModelFile(dir, {QStringLiteral("*conv*frontend*")}));
      config.qwen3EncoderFile = rel(findModelFile(dir, {QStringLiteral("*encoder*")}));
      config.qwen3DecoderFile = rel(findModelFile(dir, {QStringLiteral("*decoder*")}));
      const QString tokDir = findModelFileOrDir(dir, {QStringLiteral("tokenizer")});
      config.qwen3TokenizerDir = tokDir.isEmpty() ? modelDir : tokDir;
      break;
    }
    }
  }

  // Check for punctuation model at well-known locations (common to both paths)
  const QStringList punctCandidates = {
      // float32 model
      QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
          .filePath(QStringLiteral("punctuation/model.onnx")),
      QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
          .filePath(QStringLiteral("models/sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12-int8/model.onnx")),
      // int8 model
      QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
          .filePath(QStringLiteral("punctuation/model.int8.onnx")),
      QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
          .filePath(QStringLiteral("models/sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12-int8/model.int8.onnx")),
  };
  for (const auto &p : punctCandidates) {
    if (QFileInfo::exists(p)) {
      config.punctuationModelPath = p;
      break;
    }
  }

  return config;
}

void AsrService::loadModel() {
  if (m_modelDir.isEmpty()) {
    spdlog::warn("AsrService: cannot load model, directory not set");
    emit modelLoadResult(false, tr("Model directory not set."));
    return;
  }

  SpeechRecognizer::Config config = detectAndConfigure(m_modelDir);
  m_streamingMode = (config.type == SpeechRecognizer::Type::StreamingTransducer ||
                     config.type == SpeechRecognizer::Type::StreamingParaformer);

  QString error;
  if (!m_recognizer->start(config, &error)) {
    spdlog::error("AsrService: model load failed: {}", error);
    m_modelLoaded = false;
    emit modelLoadResult(false, error);
    return;
  }

  m_modelLoaded = true;
  const char *mode = m_streamingMode ? "streaming" : "offline";
  spdlog::info("AsrService: {} model loaded from {}", mode, m_modelDir);
  emit modelLoadResult(true, {});
}

void AsrService::unloadModel() {
  if (m_recognizer->isRunning())
    m_recognizer->stop();
  m_modelLoaded = false;
  spdlog::info("AsrService: model unloaded");
}

void AsrService::startSession() {
  if (!m_modelLoaded) {
    spdlog::warn("AsrService: startSession called but model not loaded");
    return;
  }

  if (m_recognizer->isRunning()) {
    m_recognizer->resetStream();
  }
}

void AsrService::feedAudio(const QByteArray &pcm16, int sampleRate,
                            int channels) {
  if (!m_modelLoaded)
    return;
  m_recognizer->acceptPcm16(pcm16, sampleRate, channels);
}

void AsrService::finishSession() {
  if (!m_modelLoaded)
    return;

  if (m_recognizer->isRunning()) {
    m_recognizer->finish();
    m_recognizer->resetStream();
  }
}

void AsrService::abortSession() {
  if (m_modelLoaded && m_recognizer->isRunning()) {
    m_recognizer->resetStream();
  }
}

} // namespace talkinput
