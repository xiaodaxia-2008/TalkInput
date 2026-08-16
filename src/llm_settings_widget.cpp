#include "llm_settings_widget.h"
#include "app_config.h"
#include "logging.h"
#include "utils.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QVBoxLayout>

namespace talkinput
{

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

    m_group = new QGroupBox(content);
    auto *grid = new QGridLayout(m_group);
    grid->setContentsMargins(16, 20, 16, 14);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(10);

    m_providerFormLabel = new QLabel(m_group);
    grid->addWidget(m_providerFormLabel, 0, 0);

    m_providerCombo = new QComboBox(m_group);
    m_providerCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    grid->addWidget(m_providerCombo, 0, 1);

    m_endpointFormLabel = new QLabel(m_group);
    m_endpointFormLabel->setSizePolicy(QSizePolicy::Maximum,
                                       QSizePolicy::Preferred);
    grid->addWidget(m_endpointFormLabel, 0, 2);

    m_endpointEdit = new QLineEdit(m_group);
    m_endpointEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_endpointEdit->setPlaceholderText(
        QStringLiteral("https://api.openai.com"));
    grid->addWidget(m_endpointEdit, 0, 3);

    m_llmModelFormLabel = new QLabel(m_group);
    grid->addWidget(m_llmModelFormLabel, 1, 0);

    m_llmModelCombo = new QComboBox(m_group);
    m_llmModelCombo->setEditable(true);
    m_llmModelCombo->setInsertPolicy(QComboBox::NoInsert);
    m_llmModelCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_llmModelCombo->lineEdit()->setPlaceholderText(
        tr("Model name sent to the LLM service"));
    grid->addWidget(m_llmModelCombo, 1, 1);

    m_apiKeyFormLabel = new QLabel(m_group);
    m_apiKeyFormLabel->setSizePolicy(QSizePolicy::Maximum,
                                     QSizePolicy::Preferred);
    grid->addWidget(m_apiKeyFormLabel, 1, 2);

    m_apiKeyEdit = new QLineEdit(m_group);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(QStringLiteral("sk-..."));
    m_apiKeyEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    grid->addWidget(m_apiKeyEdit, 1, 3);

    m_promptFormLabel = new QLabel(m_group);
    grid->addWidget(m_promptFormLabel, 2, 0);

    auto *promptRow = new QHBoxLayout;
    promptRow->setSpacing(8);
    m_promptLabel = new QLabel(m_group);
    m_promptLabel->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Preferred);
    m_promptLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    promptRow->addWidget(m_promptLabel, 1);

    m_promptEditButton = new QPushButton(m_group);
    m_promptEditButton->setToolTip(tr("Edit user prompt"));
    m_promptEditButton->setFlat(true);
    promptRow->addWidget(m_promptEditButton);
    grid->addLayout(promptRow, 2, 1, 1, 3);

    contentLayout->addWidget(m_group);
    contentLayout->addStretch();

    scroll->setWidget(content);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

    const int iconSize = fontMetrics().height();
    setButtonIcon(m_promptEditButton, ":/resources/icons/edit.svg", iconSize);
    m_promptEditButton->setProperty("buttonRole", "icon");

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

    connect(m_promptEditButton, &QPushButton::clicked, this,
            &LlmSettingsWidget::onEditPrompt);
}

void LlmSettingsWidget::retranslate()
{
    m_group->setTitle(tr("LLM Service"));
    m_providerFormLabel->setText(tr("Provider"));
    m_endpointFormLabel->setText(tr("Endpoint"));
    m_llmModelFormLabel->setText(tr("Model"));
    m_apiKeyFormLabel->setText(tr("API Key"));
    m_promptFormLabel->setText(tr("Prompt"));
    refreshPromptLabel();
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

    refreshPromptLabel();
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

void LlmSettingsWidget::refreshPromptLabel()
{
    const QString text =
        QString::fromStdString(appConfig().settings.llmUserPrompt);
    m_promptLabel->setText(
        QStringLiteral("%1 …").arg(text.simplified().left(50)));
    m_promptLabel->setToolTip(text);
}

void LlmSettingsWidget::onEditPrompt()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("User Prompt"));
    dialog.resize(580, 360);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    const QString hint =
        tr("Available variables: {{input}}, {{context}}, {{hotwords}}");
    auto *label = new QLabel(QStringLiteral("<b>%1</b><br><small>%2</small>")
                                 .arg(tr("User Prompt"), hint),
                             &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *editor = new QTextEdit(&dialog);
    editor->setAcceptRichText(false);
    editor->setPlaceholderText(
        tr("Use {{input}}, {{context}}, and {{hotwords}} as needed"));
    editor->setPlainText(
        QString::fromStdString(appConfig().settings.llmUserPrompt));
    layout->addWidget(editor, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString text = editor->toPlainText().trimmed();
    appConfig().settings.llmUserPrompt = text.toStdString();
    markConfigDirty();
    refreshPromptLabel();
    STATUSBAR_INFO("{}", tr("LLM prompt saved"));
}

} // namespace talkinput
