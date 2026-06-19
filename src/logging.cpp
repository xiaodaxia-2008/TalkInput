#include "logging.h"

#include <QMetaObject>
#include <QPointer>
#include <QStatusBar>
#include <QString>
#include <QStyle>
#include <QWidget>

#include <memory>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace talkinput
{
namespace
{

QString statusLevelName(spdlog::level::level_enum level)
{
    switch (level) {
    case spdlog::level::warn:
        return QStringLiteral("warning");
    case spdlog::level::err:
    case spdlog::level::critical:
        return QStringLiteral("error");
    default:
        return QStringLiteral("info");
    }
}

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
        const QString level = statusLevelName(msg.level);
        QPointer<QStatusBar> statusBar = target;
        QMetaObject::invokeMethod(
            target,
            [statusBar, text, level]() {
                if (statusBar) {
                    statusBar->setProperty("messageLevel", level);
                    statusBar->style()->unpolish(statusBar);
                    statusBar->style()->polish(statusBar);
                    for (auto *child : statusBar->findChildren<QWidget *>()) {
                        child->style()->unpolish(child);
                        child->style()->polish(child);
                    }
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

void installStatusBarLogger(QStatusBar *statusBar)
{
    auto statusBarSink = std::make_shared<StatusBarSink>(statusBar);
    auto terminalSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    terminalSink->set_level(spdlog::level::info);
    terminalSink->set_pattern("[%n] [%^%l%$] %v");

    auto logger = std::make_shared<spdlog::logger>(
        "statusbar", spdlog::sinks_init_list{statusBarSink, terminalSink});
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::register_or_replace(logger);
}

} // namespace talkinput
