#include "logging.h"

#include <QMetaObject>
#include <QPointer>
#include <QStatusBar>
#include <QString>

#include <memory>
#include <mutex>
#include <spdlog/sinks/base_sink.h>

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

void installStatusBarLogger(QStatusBar *statusBar)
{
    auto logger = std::make_shared<spdlog::logger>(
        "statusbar", std::make_shared<StatusBarSink>(statusBar));
    logger->set_level(spdlog::level::info);
    logger->flush_on(spdlog::level::info);
    spdlog::register_or_replace(logger);
}

} // namespace talkinput
