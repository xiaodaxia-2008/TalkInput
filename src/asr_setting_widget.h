#pragma once

#include "json_utils.h"

#include <QString>
#include <QWidget>
#include <memory>

class QComboBox;
class QEvent;
class QLineEdit;

namespace Ui
{
class AsrSettingWidget;
}

namespace talkinput
{

class ModelDownloadManager;

class AsrSettingWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit AsrSettingWidget(QWidget *parent = nullptr);
    ~AsrSettingWidget() override;

protected:
    void changeEvent(QEvent *event) override;

signals:
    void hotwordsChanged();

private:
    void onLlmProviderChanged(int index);
    void onOcrProviderChanged(int index);
    void onAsrModelChanged(int index);
    void refreshAsrStatus();
    void onUseAsrModel();
    void onEditHotwords();
    void onEditPrompt();
    void refreshPromptLabel();

    // Init helpers
    void initLlmProviders();
    void initLlmPrompt();
    void initOcrProvider();
    void initLlmChecks();
    void initAsrModel();
    void initIcons();

    void onDownloadFinished(const QString &modelPointer);
    QString currentAsrPresetPath() const;

    std::unique_ptr<Ui::AsrSettingWidget> m_ui;
    ModelDownloadManager *m_downloadManager = nullptr;
};

} // namespace talkinput
