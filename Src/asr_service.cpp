#include "asr_service.h"
#include "speech_recognizer.h"

#include <QDir>
#include <QFileInfo>
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

bool detectModelFiles(const QString &modelDir, QString &encoder,
                      QString &decoder, QString &joiner, QString &tokens) {
  const QDir dir(modelDir);
  encoder = findModelFile(dir, {QStringLiteral("*encoder*")});
  decoder = findModelFile(dir, {QStringLiteral("*decoder*")});
  joiner = findModelFile(dir, {QStringLiteral("*joiner*")});
  tokens = findModelFile(dir, {QStringLiteral("tokens.txt")});
  return !encoder.isEmpty() && !decoder.isEmpty() && !joiner.isEmpty() &&
         !tokens.isEmpty();
}

} // namespace

namespace talkinput {

AsrService::AsrService(QObject *parent)
    : QObject(parent) {
  m_recognizer = new SpeechRecognizer();
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

void AsrService::loadModel() {
  if (m_modelDir.isEmpty()) {
    spdlog::warn("AsrService: cannot load model, directory not set");
    emit modelLoadResult(false, tr("Model directory not set."));
    return;
  }

  QString encoder, decoder, joiner, tokens;
  if (!detectModelFiles(m_modelDir, encoder, decoder, joiner, tokens)) {
    spdlog::error("AsrService: missing transducer files in {}", m_modelDir);
    emit modelLoadResult(
        false, tr("Missing encoder/decoder/joiner ONNX files in model dir."));
    return;
  }

  SpeechRecognizer::Config config;
  config.modelDir = m_modelDir;
  config.encoderFile = QDir(m_modelDir).relativeFilePath(encoder);
  config.decoderFile = QDir(m_modelDir).relativeFilePath(decoder);
  config.joinerFile = QDir(m_modelDir).relativeFilePath(joiner);
  config.sampleRate = 16000;

  QString error;
  if (!m_recognizer->start(config, &error)) {
    spdlog::error("AsrService: model load failed: {}", error);
    m_modelLoaded = false;
    emit modelLoadResult(false, error);
    return;
  }

  m_modelLoaded = true;
  spdlog::info("AsrService: model loaded from {}", m_modelDir);
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

  // If there's an existing stream (e.g. from a previous aborted session),
  // reset it to get a fresh stream
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
