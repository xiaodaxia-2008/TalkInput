#include "logging.h"
#include "utils.h"

#include <QDir>
#include <QMetaObject>
#include <QPointer>
#include <QStatusBar>
#include <QString>

#include <memory>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <QPlainTextEdit>
#include <spdlog/sinks/qt_sinks.h>

namespace talkinput
{
namespace
{

class StatusBarSink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
    explicit StatusBarSink(QStatusBar *statusBar) : m_statusBar(statusBar)
    {
        set_pattern("%v");
        set_level(spdlog::level::info);
    }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        auto *target = m_statusBar.data();
        if (!target) {
            return;
        }

        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        const QString text = QString::fromUtf8(
            formatted.data(), static_cast<qsizetype>(formatted.size()));
        QPointer<QStatusBar> statusBar = target;
        QMetaObject::invokeMethod(
            target,
            [statusBar, text]() {
                if (statusBar) {
                    statusBar->showMessage(text);
                }
            },
            Qt::QueuedConnection);
    }

    void flush_() override
    {
    }

private:
    QPointer<QStatusBar> m_statusBar;
};

} // namespace

auto getFileSink()
{
    static auto fileSink = []() {
        const QString logPath =
            QDir(appDataDir()).filePath(QStringLiteral("talkinput.log"));
        QDir().mkpath(appDataDir());
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logPath.toStdString(), 10 * 1024 * 1024, 3);
        sink->set_level(spdlog::level::debug);
        return sink;
    }();
    return fileSink;
}

void installStatusBarLogger(QStatusBar *statusBar)
{
    auto statusBarSink = std::make_shared<StatusBarSink>(statusBar);
    statusBarSink->set_level(spdlog::level::info);

    auto logger = std::make_shared<spdlog::logger>("statusbar");
    logger->set_level(spdlog::level::info);
    logger->sinks().push_back(statusBarSink);
    logger->sinks().push_back(getFileSink());
    logger->flush_on(spdlog::level::info);
    spdlog::register_or_replace(logger);
}

void installLogPanelSink(QPlainTextEdit *textEdit)
{
    auto sink = std::make_shared<spdlog::sinks::qt_sink_mt>(
        textEdit, "appendPlainText");
    sink->set_pattern("%M:%S [%l] %v");
    sink->set_level(spdlog::level::debug);

    // Attach to all registered loggers
    spdlog::apply_all([&sink](std::shared_ptr<spdlog::logger> logger) {
        logger->sinks().push_back(sink);
    });
}

void initLogger()
{
    auto logger = std::make_shared<spdlog::logger>("talkinput");
    logger->sinks().push_back(getFileSink());
    auto terminal_sink =
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    terminal_sink->set_level(spdlog::level::debug);
    logger->sinks().push_back(terminal_sink);
    logger->set_level(spdlog::level::debug);
    logger->flush_on(spdlog::level::debug);
    spdlog::register_or_replace(logger);
    spdlog::set_default_logger(logger);
}
} // namespace talkinput
