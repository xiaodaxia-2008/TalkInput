#pragma once

#include <QWidget>
#include <memory>

class QComboBox;
class QEvent;
class QGroupBox;
class QLabel;

namespace talkinput
{

/// OCR provider selection ("OCR").
class OcrSettingsWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit OcrSettingsWidget(QWidget *parent = nullptr);
    ~OcrSettingsWidget() override;

    /// Re-reads the current config and refreshes every control.
    void refreshFromConfig();

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslate();
    void onOcrProviderChanged(int index);

    QGroupBox *m_group = nullptr;
    QLabel *m_providerLabel = nullptr;
    QComboBox *m_ocrCombo = nullptr;
};

} // namespace talkinput
