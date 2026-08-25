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

class OcrImagePreview;

namespace Ui
{
class OcrSettingsWidget;
}

namespace zenny
{

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

    std::unique_ptr<Ui::OcrSettingsWidget> m_ui;
};

} // namespace zenny
