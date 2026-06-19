#pragma once

#include "qt_fmt.h"

#include <spdlog/spdlog.h>

class QStatusBar;

namespace talkinput
{

void installStatusBarLogger(QStatusBar *statusBar);

} // namespace talkinput
