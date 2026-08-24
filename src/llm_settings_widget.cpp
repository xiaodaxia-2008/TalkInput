#include "llm_settings_widget.h"
#include "app_config.h"
#include "logging.h"
#include "ui_llm_settings_widget.h"
#include "utils.h"

#include <nlohmann/json.hpp>

#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace talkinput
{

namespace
{
QUrl modelsUrl(const QString &endpoint)
{
    QUrl url(endpoint);
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')) && path.size() > 1) {
        path.chop(1);
    }
    if (path.endsWith(QStringLiteral("/chat/completions"), Qt::CaseInsensitive))
    {
        path.chop(QStringLiteral("/chat/completions").size());
    }
    if (!path.endsWith(QStringLiteral("/models"), Qt::CaseInsensitive)) {
        if (!path.endsWith(QLatin1Char('/'))) {
            path += QLatin1Char('/');
        }
        path += QStringLiteral("models");
    }
    url.setPath(path);
    return url;
}
} // namespace

LlmSettingsWidget::LlmSettingsWidget(QWidget *parent) : QWidget(parent)
{
    buildUi();
    retranslate();
    refreshFromConfig();
}

LlmSettingsWidget::~LlmSettingsWidget() = default;

void LlmSettingsWidget::buildUi()
{
    m_ui = std::make_unique<Ui::LlmSettingsWidget>();
    m_ui->setupUi(this);
    m_ui->providerCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_ui->llmModelCombo->setInsertPolicy(QComboBox::NoInsert);
    m_ui->llmModelCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_ui->promptFormLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_ui->promptLayout->setAlignment(Qt::AlignTop);
    m_ui->contentLayout->setAlignment(m_ui->promptLayout, Qt::AlignTop);

    const int iconSize = fontMetrics().height();
    setButtonIcon(m_ui->refreshModelsButton, ":/resources/icons/refresh.svg",
                  iconSize);
    m_network = new QNetworkAccessManager(this);

    // Populate — store only the provider ID
    for (const auto &[key, preset] : appConfig().llmPresets) {
        const QString name = QString::fromStdString(preset.name);
        if (!name.isEmpty()) {
            m_ui->providerCombo->addItem(name, QString::fromStdString(key));
        }
    }

    // Endpoint edited
    connect(m_ui->endpointEdit, &QLineEdit::editingFinished, this, [this]() {
        auto &preset =
            appConfig().llmPresets
                [m_ui->providerCombo->currentData().toString().toStdString()];
        preset.endpoint = m_ui->endpointEdit->text().trimmed().toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("LLM endpoint saved"));
    });

    // Model edited — commit on focus loss or popup selection
    auto saveModel = [this]() {
        auto &preset =
            appConfig().llmPresets
                [m_ui->providerCombo->currentData().toString().toStdString()];
        preset.currentModel =
            m_ui->llmModelCombo->currentText().trimmed().toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("LLM model saved"));
    };
    connect(m_ui->llmModelCombo->lineEdit(), &QLineEdit::editingFinished, this,
            saveModel);
    connect(m_ui->llmModelCombo, &QComboBox::activated, this,
            [saveModel](int) { saveModel(); });

    // API key edited
    connect(m_ui->apiKeyEdit, &QLineEdit::editingFinished, this, [this]() {
        auto &preset =
            appConfig().llmPresets
                [m_ui->providerCombo->currentData().toString().toStdString()];
        preset.apiKey = m_ui->apiKeyEdit->text().trimmed().toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("LLM API key saved"));
    });

    connect(m_ui->providerCombo, &QComboBox::currentIndexChanged, this,
            &LlmSettingsWidget::onLlmProviderChanged);
    connect(m_ui->refreshModelsButton, &QPushButton::clicked, this,
            &LlmSettingsWidget::refreshModels);

    // Prompt edited — commit on every change
    connect(m_ui->promptEdit, &QTextEdit::textChanged, this,
            &LlmSettingsWidget::onPromptChanged);
}

void LlmSettingsWidget::retranslate()
{
    m_ui->retranslateUi(this);
}

void LlmSettingsWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
}

void LlmSettingsWidget::refreshFromConfig()
{
    const QString savedLlmProviderId =
        QString::fromStdString(appConfig().settings.llmProviderId);
    const int llmProviderIndex =
        m_ui->providerCombo->findData(savedLlmProviderId);
    {
        const QSignalBlocker blocker(m_ui->providerCombo);
        if (llmProviderIndex >= 0) {
            m_ui->providerCombo->setCurrentIndex(llmProviderIndex);
        }
    }

    {
        const auto &llmPresets = appConfig().llmPresets;
        auto llmIt = llmPresets.find(
            m_ui->providerCombo->currentData().toString().toStdString());
        if (llmIt != llmPresets.end()) {
            applyLlmProviderToUi(llmIt->second);
        }
    }

    const QSignalBlocker promptBlocker(m_ui->promptEdit);
    m_ui->promptEdit->setPlainText(
        QString::fromStdString(appConfig().settings.llmUserPrompt));
}

void LlmSettingsWidget::onLlmProviderChanged(int /*index*/)
{
    const auto &llmPresets = appConfig().llmPresets;
    auto it = llmPresets.find(
        m_ui->providerCombo->currentData().toString().toStdString());
    if (it == llmPresets.end()) {
        return;
    }
    const auto &preset = it->second;

    applyLlmProviderToUi(preset);
    appConfig().settings.llmProviderId =
        m_ui->providerCombo->currentData().toString().toStdString();
    markConfigDirty();
    STATUSBAR_INFO(
        "{}",
        tr("LLM provider saved: %1").arg(m_ui->providerCombo->currentText()));
}

void LlmSettingsWidget::applyLlmProviderToUi(const LlmPreset &provider)
{
    const QSignalBlocker epBlocker(m_ui->endpointEdit);
    const QSignalBlocker mBlocker(m_ui->llmModelCombo);
    const QSignalBlocker akBlocker(m_ui->apiKeyEdit);

    m_ui->endpointEdit->setText(QString::fromStdString(provider.endpoint));

    const QString currentModel = QString::fromStdString(provider.currentModel);
    m_ui->llmModelCombo->clear();
    for (const auto &[key, info] : provider.models) {
        m_ui->llmModelCombo->addItem(QString::fromStdString(key),
                                     QString::fromStdString(key));
    }
    if (!currentModel.isEmpty() &&
        m_ui->llmModelCombo->findText(currentModel) < 0)
    {
        m_ui->llmModelCombo->addItem(currentModel, currentModel);
    }
    m_ui->llmModelCombo->setEditText(currentModel);

    m_ui->apiKeyEdit->setText(QString::fromStdString(provider.apiKey));
}

void LlmSettingsWidget::onPromptChanged()
{
    appConfig().settings.llmUserPrompt =
        m_ui->promptEdit->toPlainText().toStdString();
    markConfigDirty();
}

void LlmSettingsWidget::refreshModels()
{
    const QString providerId = m_ui->providerCombo->currentData().toString();
    auto it = appConfig().llmPresets.find(providerId.toStdString());
    if (it == appConfig().llmPresets.end()) {
        return;
    }

    auto &preset = it->second;
    preset.endpoint = m_ui->endpointEdit->text().trimmed().toStdString();
    preset.apiKey = m_ui->apiKeyEdit->text().trimmed().toStdString();
    markConfigDirty();

    const QUrl url = modelsUrl(QString::fromStdString(preset.endpoint));
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
        STATUSBAR_INFO("{}", tr("Invalid LLM endpoint"));
        return;
    }

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    const QString apiKey = QString::fromStdString(preset.apiKey);
    if (!apiKey.isEmpty()) {
        request.setRawHeader("Authorization",
                             QStringLiteral("Bearer %1").arg(apiKey).toUtf8());
    }

    m_ui->refreshModelsButton->setEnabled(false);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, providerId]() {
        m_ui->refreshModelsButton->setEnabled(true);
        const auto replyGuard =
            qScopeGuard([reply]() { reply->deleteLater(); });

        if (reply->error() != QNetworkReply::NoError) {
            STATUSBAR_INFO(
                "{}",
                tr("Failed to refresh models: %1").arg(reply->errorString()));
            return;
        }

        try {
            const QByteArray body = reply->readAll();
            const auto document = nlohmann::json::parse(
                body.constData(), body.constData() + body.size());
            if (!document.contains("data") || !document["data"].is_array()) {
                STATUSBAR_INFO("{}", tr("Model list response is invalid"));
                return;
            }

            std::map<std::string, LlmModel> models;
            for (const auto &item : document["data"]) {
                if (!item.is_object() || !item.contains("id") ||
                    !item["id"].is_string())
                {
                    continue;
                }
                const std::string id = item["id"].get<std::string>();
                if (id.empty()) {
                    continue;
                }
                LlmModel model;
                model.name = item.value("name", id);
                models.emplace(id, std::move(model));
            }
            if (models.empty()) {
                STATUSBAR_INFO("{}", tr("No models returned by endpoint"));
                return;
            }

            auto presetIt =
                appConfig().llmPresets.find(providerId.toStdString());
            if (presetIt == appConfig().llmPresets.end()) {
                return;
            }
            presetIt->second.models = std::move(models);
            markConfigDirty();
            applyLlmProviderToUi(presetIt->second);
            STATUSBAR_INFO(
                "{}",
                tr("Models refreshed: %1").arg(presetIt->second.models.size()));
        }
        catch (const nlohmann::json::exception &) {
            STATUSBAR_INFO("{}", tr("Model list response is invalid"));
        }
    });
}

} // namespace talkinput
