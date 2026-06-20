#pragma once

#include "qt_fmt.h"

#include <spdlog/spdlog.h>

#define STATUSBAR_TRACE(...)                                                   \
    SPDLOG_LOGGER_CALL(spdlog::get("statusbar"), spdlog::level::trace,         \
                       __VA_ARGS__)
#define STATUSBAR_DEBUG(...)                                                   \
    SPDLOG_LOGGER_CALL(spdlog::get("statusbar"), spdlog::level::debug,         \
                       __VA_ARGS__)
#define STATUSBAR_INFO(...)                                                    \
    SPDLOG_LOGGER_CALL(spdlog::get("statusbar"), spdlog::level::info,          \
                       __VA_ARGS__)
#define STATUSBAR_WARN(...)                                                    \
    SPDLOG_LOGGER_CALL(spdlog::get("statusbar"), spdlog::level::warn,          \
                       __VA_ARGS__)
#define STATUSBAR_ERROR(...)                                                   \
    SPDLOG_LOGGER_CALL(spdlog::get("statusbar"), spdlog::level::err,           \
                       __VA_ARGS__)
#define STATUSBAR_CRITICAL(...)                                                \
    SPDLOG_LOGGER_CALL(spdlog::get("statusbar"), spdlog::level::critical,      \
                       __VA_ARGS__)

class QStatusBar;

namespace talkinput
{

void installStatusBarLogger(QStatusBar *statusBar);

} // namespace talkinput
