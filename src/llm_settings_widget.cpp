#include "llm_settings_widget.h"
#include "app_config.h"
#include "logging.h"
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
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("settingsScroll"));

    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);

    // One setting per row
    m_providerFormLabel = new QLabel(content);
    grid->addWidget(m_providerFormLabel, 0, 0);

    m_providerCombo = new QComboBox(content);
    m_providerCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    grid->addWidget(m_providerCombo, 0, 1);

    m_endpointFormLabel = new QLabel(content);
    grid->addWidget(m_endpointFormLabel, 1, 0);

    m_endpointEdit = new QLineEdit(content);
    m_endpointEdit->setPlaceholderText(
        QStringLiteral("https://api.openai.com"));
    grid->addWidget(m_endpointEdit, 1, 1);

    m_apiKeyFormLabel = new QLabel(content);
    grid->addWidget(m_apiKeyFormLabel, 2, 0);

    m_apiKeyEdit = new QLineEdit(content);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(QStringLiteral("sk-..."));
    grid->addWidget(m_apiKeyEdit, 2, 1);

    m_llmModelFormLabel = new QLabel(content);
    grid->addWidget(m_llmModelFormLabel, 3, 0);

    m_llmModelCombo = new QComboBox(content);
    m_llmModelCombo->setEditable(true);
    m_llmModelCombo->setInsertPolicy(QComboBox::NoInsert);
    m_llmModelCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_llmModelCombo->lineEdit()->setPlaceholderText(
        tr("Model name sent to the LLM service"));
    auto *modelRow = new QHBoxLayout;
    modelRow->setSpacing(8);
    modelRow->addWidget(m_llmModelCombo, 1);
    m_refreshModelsButton = new QPushButton(content);
    m_refreshModelsButton->setFlat(true);
    modelRow->addWidget(m_refreshModelsButton);
    grid->addLayout(modelRow, 3, 1);

    // Multi-line prompt editor
    m_promptFormLabel = new QLabel(content);
    m_promptFormLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    grid->addWidget(m_promptFormLabel, 4, 0, 1, 2);

    m_promptEdit = new QTextEdit(content);
    m_promptEdit->setAcceptRichText(false);
    m_promptEdit->setFixedHeight(140);
    grid->addWidget(m_promptEdit, 5, 0, 1, 2);

    contentLayout->addLayout(grid);
    contentLayout->addStretch();

    scroll->setWidget(content);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

    const int iconSize = fontMetrics().height();
    setButtonIcon(m_refreshModelsButton, ":/resources/icons/refresh.svg",
                  iconSize);
    m_network = new QNetworkAccessManager(this);

    // Populate — store only the provider ID
    for (const auto &[key, preset] : appConfig().llmPresets) {
        const QString name = QString::fromStdString(preset.name);
        if (!name.isEmpty()) {
            m_providerCombo->addItem(name, QString::fromStdString(key));
        }
    }

    // Endpoint edited
    connect(m_endpointEdit, &QLineEdit::editingFinished, this, [this]() {
        auto &preset =
            appConfig().llmPresets
                [m_providerCombo->currentData().toString().toStdString()];
        preset.endpoint = m_endpointEdit->text().trimmed().toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("LLM endpoint saved"));
    });

    // Model edited — commit on focus loss or popup selection
    auto saveModel = [this]() {
        auto &preset =
            appConfig().llmPresets
                [m_providerCombo->currentData().toString().toStdString()];
        preset.currentModel =
            m_llmModelCombo->currentText().trimmed().toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("LLM model saved"));
    };
    connect(m_llmModelCombo->lineEdit(), &QLineEdit::editingFinished, this,
            saveModel);
    connect(m_llmModelCombo, &QComboBox::activated, this,
            [saveModel](int) { saveModel(); });

    // API key edited
    connect(m_apiKeyEdit, &QLineEdit::editingFinished, this, [this]() {
        auto &preset =
            appConfig().llmPresets
                [m_providerCombo->currentData().toString().toStdString()];
        preset.apiKey = m_apiKeyEdit->text().trimmed().toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("LLM API key saved"));
    });

    connect(m_providerCombo, &QComboBox::currentIndexChanged, this,
            &LlmSettingsWidget::onLlmProviderChanged);
    connect(m_refreshModelsButton, &QPushButton::clicked, this,
            &LlmSettingsWidget::refreshModels);

    // Prompt edited — commit on every change
    connect(m_promptEdit, &QTextEdit::textChanged, this,
            &LlmSettingsWidget::onPromptChanged);
}

void LlmSettingsWidget::retranslate()
{
    m_providerFormLabel->setText(tr("Provider"));
    m_endpointFormLabel->setText(tr("Endpoint"));
    m_llmModelFormLabel->setText(tr("Model"));
    m_apiKeyFormLabel->setText(tr("API Key"));
    m_refreshModelsButton->setText(tr("Refresh models"));
    m_refreshModelsButton->setToolTip(
        tr("Fetch models from the configured endpoint"));
    m_promptFormLabel->setText(
        QStringLiteral("<b>%1</b><br><small>%2</small>")
            .arg(tr("Prompt"),
                 tr("Available variables: {{input}}, {{context}}, "
                    "{{hotwords}}")));
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
    const int llmProviderIndex = m_providerCombo->findData(savedLlmProviderId);
    {
        const QSignalBlocker blocker(m_providerCombo);
        if (llmProviderIndex >= 0) {
            m_providerCombo->setCurrentIndex(llmProviderIndex);
        }
    }

    {
        const auto &llmPresets = appConfig().llmPresets;
        auto llmIt = llmPresets.find(
            m_providerCombo->currentData().toString().toStdString());
        if (llmIt != llmPresets.end()) {
            applyLlmProviderToUi(llmIt->second);
        }
    }

    const QSignalBlocker promptBlocker(m_promptEdit);
    m_promptEdit->setPlainText(
        QString::fromStdString(appConfig().settings.llmUserPrompt));
}

void LlmSettingsWidget::onLlmProviderChanged(int /*index*/)
{
    const auto &llmPresets = appConfig().llmPresets;
    auto it = llmPresets.find(
        m_providerCombo->currentData().toString().toStdString());
    if (it == llmPresets.end()) {
        return;
    }
    const auto &preset = it->second;

    applyLlmProviderToUi(preset);
    appConfig().settings.llmProviderId =
        m_providerCombo->currentData().toString().toStdString();
    markConfigDirty();
    STATUSBAR_INFO(
        "{}", tr("LLM provider saved: %1").arg(m_providerCombo->currentText()));
}

void LlmSettingsWidget::applyLlmProviderToUi(const LlmPreset &provider)
{
    const QSignalBlocker epBlocker(m_endpointEdit);
    const QSignalBlocker mBlocker(m_llmModelCombo);
    const QSignalBlocker akBlocker(m_apiKeyEdit);

    m_endpointEdit->setText(QString::fromStdString(provider.endpoint));

    const QString currentModel = QString::fromStdString(provider.currentModel);
    m_llmModelCombo->clear();
    for (const auto &[key, info] : provider.models) {
        m_llmModelCombo->addItem(QString::fromStdString(key),
                                 QString::fromStdString(key));
    }
    if (!currentModel.isEmpty() && m_llmModelCombo->findText(currentModel) < 0)
    {
        m_llmModelCombo->addItem(currentModel, currentModel);
    }
    m_llmModelCombo->setEditText(currentModel);

    m_apiKeyEdit->setText(QString::fromStdString(provider.apiKey));
}

void LlmSettingsWidget::onPromptChanged()
{
    appConfig().settings.llmUserPrompt =
        m_promptEdit->toPlainText().toStdString();
    markConfigDirty();
}

void LlmSettingsWidget::refreshModels()
{
    const QString providerId = m_providerCombo->currentData().toString();
    auto it = appConfig().llmPresets.find(providerId.toStdString());
    if (it == appConfig().llmPresets.end()) {
        return;
    }

    auto &preset = it->second;
    preset.endpoint = m_endpointEdit->text().trimmed().toStdString();
    preset.apiKey = m_apiKeyEdit->text().trimmed().toStdString();
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

    m_refreshModelsButton->setEnabled(false);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, providerId]() {
        m_refreshModelsButton->setEnabled(true);
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
