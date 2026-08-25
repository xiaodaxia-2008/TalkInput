#pragma once

#include <QCoro/QCoroTask>

#include <QWidget>
#include <memory>

class QComboBox;
class QAction;
class QEvent;
class QLabel;
class QPushButton;
class QTextEdit;

namespace Ui
{
class SttSettingsWidget;
}

namespace talkinput
{

/// Speech-to-text settings: model, mode, hot words, actions, result and
/// behavior.
class SttSettingsWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SttSettingsWidget(QWidget *parent = nullptr);
    ~SttSettingsWidget() override;

    /// Re-reads the current config and refreshes every control.
    void refreshFromConfig();
    void setRecognitionActions(QAction *startAction, QAction *fileAction);
    void setRecognitionResult(const QString &text);

    /// Synchronizes the active-mode combo with the current config.
    void updateActiveModeDisplay();

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslate();
    void initAsrModel();
    void initActiveMode();

    void onUseAsrModel();
    void onOpenModelUrl();
    void onImportModel();
    void onHotwordsChanged();
    void saveHotwords(bool reloadModel);

    QCoro::Task<void> useAsrModel(const QString &providerId);
    QCoro::Task<bool> downloadAsrModel(const QString &providerId);
    void loadInstalledAsrModel(const QString &providerId);
    void refreshAsrModelCombo();

    std::unique_ptr<Ui::SttSettingsWidget> m_ui;
};

} // namespace talkinput
