#pragma once

#include <QCoro/QCoroTask>

#include <QWidget>
#include <memory>

class QComboBox;
class QEvent;
class QGroupBox;
class QLabel;
class QPushButton;

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

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslate();
    void initAsrModel();

    void onUseAsrModel();
    void onOpenModelUrl();
    void onImportModel();
    void onEditHotwords();
    void onOpenMoreModels();

    QCoro::Task<void> useAsrModel(const QString &providerId);
    QCoro::Task<bool> downloadAsrModel(const QString &providerId);
    void loadInstalledAsrModel(const QString &providerId);
    void refreshAsrModelCombo();

    QGroupBox *m_modelGroup = nullptr;
    QGroupBox *m_hotwordsGroup = nullptr;
    QLabel *m_modelLabel = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QPushButton *m_browserButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_useButton = nullptr;
    QPushButton *m_hotwordsButton = nullptr;
    QPushButton *m_moreModelsButton = nullptr;
    QLabel *m_hotwordsHintLabel = nullptr;
};

} // namespace talkinput
