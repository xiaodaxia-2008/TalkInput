#include "llm_config.h"
#include "app_config.h"

namespace talkinput
{

nlohmann::json llmPresets()
{
    return appConfigValue("/llmPresets");
}

nlohmann::json firstLlmProviderPreset()
{
    const nlohmann::json providers = llmPresets();
    if (providers.is_object() && !providers.empty()) {
        return providers.begin().value();
    }
    return nlohmann::json::object();
}

nlohmann::json llmProviderPreset(const QString &id)
{
    if (id.isEmpty()) {
        return nlohmann::json::object();
    }
    return appConfigValue(("/llmPresets/" + id).toStdString());
}

QString currentLlmProviderId()
{
    return appConfigString("/settings/llm/providerId").trimmed();
}

nlohmann::json currentLlmProviderPreset()
{
    const nlohmann::json provider = llmProviderPreset(currentLlmProviderId());
    if (provider.is_object() && !provider.empty()) {
        return provider;
    }
    return firstLlmProviderPreset();
}

void setCurrentLlmProviderId(const QString &id)
{
    setAppConfigValue("/settings/llm/providerId", id.toStdString());
}

void setLlmProviderSetting(const QString &id, const QString &key,
                           const nlohmann::json &value)
{
    if (id.isEmpty() || key.isEmpty()) {
        return;
    }

    setAppConfigValue(
        QStringLiteral("/llmPresets/%1/%2").arg(id, key).toStdString(), value);
}

QString llmProviderEndpoint(const nlohmann::json &provider)
{
    return jsonString(provider, "endpoint").trimmed();
}

QString llmProviderModel(const nlohmann::json &provider)
{
    return jsonString(provider, "currentModel").trimmed();
}

QString llmProviderApiKey(const nlohmann::json &provider)
{
    return jsonString(provider, "apiKey").trimmed();
}

bool llmProviderUsesManagedLocalService(const nlohmann::json &provider)
{
    return provider.value("managedLocalService", false);
}

} // namespace talkinput
