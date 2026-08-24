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

namespace talkinput
{

/// Speech recognition: model selection + hot words ("模型与热词").
class RecognitionModelWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit RecognitionModelWidget(QWidget *parent = nullptr);
    ~RecognitionModelWidget() override;

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

    QLabel *m_modeLabel = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QLabel *m_modelLabel = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QPushButton *m_browserButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_useButton = nullptr;
    QPushButton *m_hotwordsButton = nullptr;
    QPushButton *m_startRecognitionButton = nullptr;
    QPushButton *m_recognizeFileButton = nullptr;
    QLabel *m_hotwordsHintLabel = nullptr;
    QTextEdit *m_hotwordsEdit = nullptr;
    QPushButton *m_hotwordsSaveButton = nullptr;
    QTextEdit *m_resultEdit = nullptr;
};

} // namespace talkinput
