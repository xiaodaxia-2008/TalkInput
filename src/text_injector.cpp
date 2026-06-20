#include "text_injector.h"
#include "logging.h"
#include "paste_text.h"

namespace talkinput
{

TextInjector::TextInjector(QObject *parent) : QObject(parent)
{
}

bool TextInjector::inject(const QString &text)
{
    if (text.isEmpty()) {
        return false;
    }

    SPDLOG_INFO("Sending text to foreground app: {}", text);
    pasteTextToActiveWindow(text, true, false);
    return true;
}

} // namespace talkinput
