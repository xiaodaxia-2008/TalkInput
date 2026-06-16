#pragma once

#include <QMetaObject>
#include <QString>
#include <QTextEdit>

#include <spdlog/sinks/base_sink.h>
#include <mutex>

namespace talkinput {

class QtSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
  explicit QtSink(QTextEdit *logView) : m_logView(logView) {}

protected:
  void sink_it_(const spdlog::details::log_msg &msg) override {
    if (!m_logView) {
      return;
    }

    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);

    QString text = QString::fromUtf8(
        formatted.data(), static_cast<qsizetype>(formatted.size()));
    text = text.trimmed();

    if (!text.isEmpty()) {
      QMetaObject::invokeMethod(
          m_logView, [view = m_logView, text]() { view->append(text); },
          Qt::QueuedConnection);
    }
  }

  void flush_() override {}

private:
  QTextEdit *m_logView = nullptr;
};

} // namespace talkinput
