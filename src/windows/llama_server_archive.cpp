#include "../app_config.h"
#include "../json_utils.h"
#include "../llm_config.h"
#include "../spawn_llama_server.h"

namespace talkinput
{

QString llamaServerExecutableName()
{
    return QStringLiteral("llama-server.exe");
}

QUrl llamaServerArchiveUrl()
{
    const auto provider = currentLlmProviderPreset();
    const nlohmann::json urls =
        provider.value("localServiceArchiveUrl", nlohmann::json::object());
    return QUrl(jsonString(urls, "windows"));
}

} // namespace talkinput
