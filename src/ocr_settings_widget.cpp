#include "ocr_settings_widget.h"
#include "app_config.h"
#include "logging.h"
#include "ocr_recognizer.h"
#include "voice_input_controller.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace talkinput
{

class OcrImagePreview final : public QLabel
{
public:
    explicit OcrImagePreview(QWidget *parent = nullptr) : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void setImage(const QImage &image)
    {
        m_image = image;
        if (m_image.isNull()) {
            clear();
            return;
        }
        setText({});
        updatePixmap();
    }

    QSize sizeHint() const override
    {
        return {640, 360};
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updatePixmap();
    }

private:
    void updatePixmap()
    {
        if (m_image.isNull() || contentsRect().isEmpty()) {
            return;
        }
        setPixmap(QPixmap::fromImage(m_image).scaled(contentsRect().size(),
                                                     Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation));
    }

    QImage m_image;
};

OcrSettingsWidget::OcrSettingsWidget(QWidget *parent) : QWidget(parent)
{
    buildUi();
    retranslate();
    refreshFromConfig();
}

OcrSettingsWidget::~OcrSettingsWidget() = default;

void OcrSettingsWidget::buildUi()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("settingsScroll"));

    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    auto *groupLayout = new QHBoxLayout();
    groupLayout->setSpacing(8);

    m_providerLabel = new QLabel(content);
    groupLayout->addWidget(m_providerLabel);

    m_ocrCombo = new QComboBox(content);
    m_ocrCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    groupLayout->addWidget(m_ocrCombo, 1);

    contentLayout->addLayout(groupLayout);

    auto *actionsRow = new QHBoxLayout;
    actionsRow->setSpacing(8);
    m_clipboardButton = new QPushButton(content);
    m_openImageButton = new QPushButton(content);
    actionsRow->addWidget(m_clipboardButton);
    actionsRow->addWidget(m_openImageButton);
    actionsRow->addStretch();
    contentLayout->addLayout(actionsRow);

    auto *resultHeader = new QHBoxLayout;
    m_resultLabel = new QLabel(content);
    resultHeader->addWidget(m_resultLabel);
    resultHeader->addStretch();
    m_copyResultButton = new QPushButton(content);
    resultHeader->addWidget(m_copyResultButton);
    contentLayout->addLayout(resultHeader);

    m_resultEdit = new QTextEdit(content);
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setMinimumHeight(100);
    contentLayout->addWidget(m_resultEdit);

    m_previewLabel = new OcrImagePreview(content);
    m_previewLabel->setFrameShape(QFrame::Box);
    m_previewLabel->setFrameShadow(QFrame::Sunken);
    m_previewLabel->setLineWidth(1);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumSize(320, 220);
    m_previewLabel->setText(tr("OCR image preview will appear here"));
    contentLayout->addWidget(m_previewLabel);

    contentLayout->addStretch();

    scroll->setWidget(content);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

    for (const auto &[key, preset] : appConfig().ocrPresets) {
        m_ocrCombo->addItem(QString::fromStdString(preset.name),
                            QString::fromStdString(key));
    }

    connect(m_ocrCombo, &QComboBox::currentIndexChanged, this,
            &OcrSettingsWidget::onOcrProviderChanged);
    connect(m_clipboardButton, &QPushButton::clicked, this,
            &OcrSettingsWidget::recognizeClipboardImage);
    connect(m_openImageButton, &QPushButton::clicked, this,
            &OcrSettingsWidget::openImageAndRecognize);
    connect(m_copyResultButton, &QPushButton::clicked, this,
            &OcrSettingsWidget::copyResult);
}

void OcrSettingsWidget::retranslate()
{
    m_providerLabel->setText(tr("Provider:"));
    m_clipboardButton->setText(tr("Recognize clipboard image"));
    m_openImageButton->setText(tr("Open image and recognize"));
    m_resultLabel->setText(tr("OCR Result"));
    m_copyResultButton->setText(tr("Copy result"));
    m_copyResultButton->setToolTip(tr("Copy OCR result to clipboard"));
    m_previewLabel->setText(tr("OCR image preview will appear here"));
}

void OcrSettingsWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
}

void OcrSettingsWidget::refreshFromConfig()
{
    const int index = m_ocrCombo->findData(
        QString::fromStdString(appConfig().settings.ocrProviderId));
    const QSignalBlocker blocker(m_ocrCombo);
    if (index >= 0) {
        m_ocrCombo->setCurrentIndex(index);
    }
}

void OcrSettingsWidget::onOcrProviderChanged(int /*index*/)
{
    appConfig().settings.ocrProviderId =
        m_ocrCombo->currentData().toString().toStdString();
    markConfigDirty();

    if (auto *vc = VoiceInputController::instance()) {
        vc->reloadOcrRecognizer();
    }
}

void OcrSettingsWidget::recognizeClipboardImage()
{
    const QImage image = QApplication::clipboard()->image();
    if (image.isNull()) {
        STATUSBAR_INFO("{}", tr("The clipboard does not contain an image."));
        return;
    }
    recognizeImage(image);
}

void OcrSettingsWidget::openImageAndRecognize()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open image"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    const QImage image(path);
    if (image.isNull()) {
        STATUSBAR_INFO("{}", tr("Failed to open image."));
        return;
    }
    recognizeImage(image);
}

void OcrSettingsWidget::recognizeImage(const QImage &image)
{
    auto *controller = VoiceInputController::instance();
    if (!controller) {
        STATUSBAR_INFO("{}", tr("OCR provider is not available."));
        return;
    }

    m_resultEdit->clear();
    m_copyResultButton->setEnabled(false);
    showPreview(image, {});
    m_clipboardButton->setEnabled(false);
    m_openImageButton->setEnabled(false);

    const QPointer<OcrSettingsWidget> guard(this);
    controller->submitDetailedOcr(
        image, [guard, image](const ApiOcrResult &result) {
            if (!guard) {
                return;
            }
            guard->m_clipboardButton->setEnabled(true);
            guard->m_openImageButton->setEnabled(true);
            if (!result.error.isEmpty()) {
                STATUSBAR_INFO("{}", result.error);
                return;
            }
            guard->m_resultEdit->setPlainText(result.text);
            guard->m_copyResultButton->setEnabled(!result.text.isEmpty());
            guard->showPreview(image, result.blocks);
        });
}

void OcrSettingsWidget::copyResult()
{
    const QString text = m_resultEdit->toPlainText();
    if (text.isEmpty()) {
        return;
    }
    QApplication::clipboard()->setText(text);
    STATUSBAR_INFO("{}", tr("OCR result copied"));
}

void OcrSettingsWidget::showPreview(const QImage &image,
                                    const QVector<OcrTextBlock> &blocks)
{
    if (image.isNull()) {
        m_previewLabel->setImage({});
        return;
    }

    constexpr int maxImageWidth = 1920;
    constexpr int maxImageHeight = 1080;
    const QSize maxImageSize(maxImageWidth, maxImageHeight);
    const QImage displayImage =
        image.size().boundedTo(maxImageSize) == image.size()
            ? image
            : image.scaled(maxImageSize, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation);
    const double scaleX =
        static_cast<double>(displayImage.width()) / std::max(1, image.width());
    const double scaleY = static_cast<double>(displayImage.height()) /
                          std::max(1, image.height());

    QImage annotated = displayImage.convertToFormat(QImage::Format_ARGB32);
    QPainter painter(&annotated);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF imageRect(0, 0, annotated.width(), annotated.height());
    for (const auto &block : blocks) {
        QRectF bounds = block.bounds;
        bounds.setLeft(bounds.left() * scaleX);
        bounds.setTop(bounds.top() * scaleY);
        bounds.setWidth(bounds.width() * scaleX);
        bounds.setHeight(bounds.height() * scaleY);
        bounds = bounds.intersected(imageRect);
        if (bounds.isEmpty()) {
            continue;
        }
        painter.fillRect(bounds, QColor(255, 245, 120, 130));
        painter.setPen(
            QPen(QColor(210, 40, 40), std::max(1, annotated.width() / 1200)));
        QFont font = painter.font();
        font.setPixelSize(
            std::clamp(static_cast<int>(bounds.height() * 0.7), 12, 64));
        painter.setFont(font);
        painter.drawText(bounds, Qt::AlignCenter | Qt::TextWordWrap,
                         block.text);
    }
    painter.end();

    m_previewLabel->setImage(annotated);
    m_previewLabel->setMinimumSize(320, 220);
}

} // namespace talkinput
