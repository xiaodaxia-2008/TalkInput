#include "logging.h"
#include "utils.h"

#include <QDir>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QSizePolicy>
#include <QStatusBar>
#include <QString>
#include <QStyle>

#include <memory>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
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
    explicit StatusBarSink(QLabel *label) : m_label(label)
    {
        set_pattern("%v");
        set_level(spdlog::level::info);
    }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        auto *target = m_label.data();
        if (!target) {
            return;
        }

        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        const QString text = QString::fromUtf8(
            formatted.data(), static_cast<qsizetype>(formatted.size()));
        const QString level = statusLevelName(msg.level);
        QPointer<QLabel> label = target;
        QMetaObject::invokeMethod(
            target,
            [label, text, level]() {
                if (label) {
                    label->setProperty("messageLevel", level);
                    label->style()->unpolish(label);
                    label->style()->polish(label);
                    label->setText(text);
                }
            },
            Qt::QueuedConnection);
    }

    void flush_() override
    {
    }

private:
    QPointer<QLabel> m_label;
};

} // namespace

auto getFileSink()
{
    static auto fileSink = []() {
        const QString logPath =
            QDir(appDataDir()).filePath(QStringLiteral("talkinput.log"));
        auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            logPath.toStdString(), 10 * 1024 * 1024, 3);
        sink->set_level(spdlog::level::debug);
        return sink;
    }();
    return fileSink;
}

void installStatusBarLogger(QStatusBar *statusBar)
{
    auto *statusLabel = statusBar->findChild<QLabel *>("statusbarLogLabel");
    if (!statusLabel) {
        statusLabel = new QLabel(statusBar);
        statusLabel->setObjectName(QStringLiteral("statusbarLogLabel"));
        statusLabel->setSizePolicy(QSizePolicy::Expanding,
                                   QSizePolicy::Preferred);
        statusBar->addWidget(statusLabel, 1);
    }

    auto statusBarSink = std::make_shared<StatusBarSink>(statusLabel);
    statusBarSink->set_level(spdlog::level::info);

    auto logger = std::make_shared<spdlog::logger>("statusbar");
    logger->set_level(spdlog::level::info);
    logger->sinks().push_back(statusBarSink);
    logger->sinks().push_back(getFileSink());
    logger->flush_on(spdlog::level::info);
    spdlog::register_or_replace(logger);
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
