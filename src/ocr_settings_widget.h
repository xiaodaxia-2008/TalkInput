#pragma once

#include <QVector>
#include <QWidget>
#include <memory>

class QComboBox;
class QEvent;
class QImage;
class QLabel;
class QPushButton;
class QTextEdit;

namespace talkinput
{

class OcrImagePreview;
struct OcrTextBlock;

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
    void recognizeClipboardImage();
    void openImageAndRecognize();
    void recognizeImage(const QImage &image);
    void copyResult();
    void showPreview(const QImage &image, const QVector<OcrTextBlock> &blocks);

    QLabel *m_providerLabel = nullptr;
    QComboBox *m_ocrCombo = nullptr;
    QPushButton *m_clipboardButton = nullptr;
    QPushButton *m_openImageButton = nullptr;
    QLabel *m_resultLabel = nullptr;
    QPushButton *m_copyResultButton = nullptr;
    QTextEdit *m_resultEdit = nullptr;
    OcrImagePreview *m_previewLabel = nullptr;
};

} // namespace talkinput
