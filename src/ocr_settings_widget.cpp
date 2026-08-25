#include "ocr_settings_widget.h"
#include "app_config.h"
#include "logging.h"
#include "ocr_recognizer.h"
#include "voice_pipeline_controller.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFontMetricsF>
#include <QFrame>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTextEdit>
#include <QTextOption>
#include <QVBoxLayout>

#include <algorithm>

namespace zenny
{

} // namespace zenny

class OcrImagePreview final : public QGraphicsView
{
public:
    explicit OcrImagePreview(QWidget *parent = nullptr)
        : QGraphicsView(parent), m_scene(new QGraphicsScene(this))
    {
        setScene(m_scene);
        setAlignment(Qt::AlignCenter);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(320, 220);
        setFrameShape(QFrame::Box);
        setFrameShadow(QFrame::Sunken);
        setLineWidth(1);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setBackgroundBrush(palette().base());
    }

    void setPlaceholder(const QString &text)
    {
        m_hasImage = false;
        m_scene->clear();
        auto *item = m_scene->addText(text);
        QFont placeholderFont = item->font();
        placeholderFont.setPixelSize(18);
        item->setFont(placeholderFont);
        item->setDefaultTextColor(palette().text().color());
        m_scene->setSceneRect(QRectF(0, 0, 640, 360));
        item->setPos((640 - item->boundingRect().width()) / 2,
                     (360 - item->boundingRect().height()) / 2);
        fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    }

    bool hasImage() const
    {
        return m_hasImage;
    }

    void setContent(const QImage &image,
                    const QVector<zenny::OcrTextBlock> &blocks)
    {
        m_scene->clear();
        if (image.isNull()) {
            m_hasImage = false;
            m_scene->setSceneRect({});
            return;
        }
        m_hasImage = true;

        const QPixmap pixmap = QPixmap::fromImage(image);
        m_scene->addPixmap(pixmap);
        m_scene->setSceneRect(QRectF(QPointF(0, 0), image.size()));
        QVector<QRectF> occupiedBounds;

        for (const auto &block : blocks) {
            const QRectF bounds =
                block.bounds.intersected(QRectF(QPointF(0, 0), image.size()));
            if (bounds.isEmpty() || block.text.trimmed().isEmpty()) {
                continue;
            }
            const double boundsArea = bounds.width() * bounds.height();
            bool duplicate = false;
            for (const QRectF &occupied : occupiedBounds) {
                const QRectF intersection = bounds.intersected(occupied);
                const double intersectionArea =
                    intersection.width() * intersection.height();
                const double smallerArea =
                    std::min(boundsArea, occupied.width() * occupied.height());
                if (smallerArea > 0.0 && intersectionArea / smallerArea > 0.5) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                continue;
            }
            occupiedBounds.append(bounds);

            auto *background = m_scene->addRect(
                bounds, QPen(Qt::NoPen), QBrush(QColor(255, 245, 120, 90)));
            background->setZValue(1);
            background->setAcceptedMouseButtons(Qt::NoButton);

            auto *textItem = m_scene->addText(block.text);
            QFont font = textItem->font();
            int fontSize =
                std::clamp(static_cast<int>(bounds.height() * 0.72), 8, 64);
            while (fontSize > 8) {
                font.setPixelSize(fontSize);
                if (QFontMetricsF(font).horizontalAdvance(block.text) <=
                    bounds.width())
                {
                    break;
                }
                --fontSize;
            }
            textItem->setFont(font);
            QTextOption textOption;
            textOption.setAlignment(Qt::AlignCenter);
            textOption.setWrapMode(QTextOption::NoWrap);
            textItem->document()->setDefaultTextOption(textOption);
            textItem->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                              Qt::TextSelectableByKeyboard);
            textItem->setFlag(QGraphicsItem::ItemIsSelectable, true);
            textItem->setDefaultTextColor(Qt::black);
            textItem->setTextWidth(bounds.width());
            textItem->setPos(bounds.topLeft());
            textItem->setZValue(2);
        }

        fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    }

    QSize sizeHint() const override
    {
        return {640, 360};
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QGraphicsView::resizeEvent(event);
        if (!m_scene->sceneRect().isEmpty()) {
            fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
        }
    }

private:
    QGraphicsScene *m_scene = nullptr;
    bool m_hasImage = false;
};

#include "ui_ocr_settings_widget.h"

namespace zenny
{

OcrSettingsWidget::OcrSettingsWidget(QWidget *parent) : QWidget(parent)
{
    buildUi();
    retranslate();
    refreshFromConfig();
}

OcrSettingsWidget::~OcrSettingsWidget() = default;

void OcrSettingsWidget::buildUi()
{
    m_ui = std::make_unique<Ui::OcrSettingsWidget>();
    m_ui->setupUi(this);
    m_ui->copyResultButton->setEnabled(false);
    m_ui->clipboardButton->setProperty("buttonRole", "icon");
    m_ui->openImageButton->setProperty("buttonRole", "icon");
    m_ui->copyResultButton->setProperty("buttonRole", "icon");
    m_ui->previewLabel->setPlaceholder(
        tr("OCR image preview will appear here"));

    for (const auto &[key, preset] : appConfig().ocrPresets) {
        m_ui->ocrCombo->addItem(QString::fromStdString(preset.name),
                                QString::fromStdString(key));
    }

    connect(m_ui->ocrCombo, &QComboBox::currentIndexChanged, this,
            &OcrSettingsWidget::onOcrProviderChanged);
    connect(m_ui->clipboardButton, &QPushButton::clicked, this,
            &OcrSettingsWidget::recognizeClipboardImage);
    connect(m_ui->openImageButton, &QPushButton::clicked, this,
            &OcrSettingsWidget::openImageAndRecognize);
    connect(m_ui->copyResultButton, &QPushButton::clicked, this,
            &OcrSettingsWidget::copyResult);
}

void OcrSettingsWidget::retranslate()
{
    m_ui->retranslateUi(this);
    if (!m_ui->previewLabel->hasImage()) {
        m_ui->previewLabel->setPlaceholder(
            tr("OCR image preview will appear here"));
    }
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
    const int index = m_ui->ocrCombo->findData(
        QString::fromStdString(appConfig().settings.ocrProviderId));
    const QSignalBlocker blocker(m_ui->ocrCombo);
    if (index >= 0) {
        m_ui->ocrCombo->setCurrentIndex(index);
    }
}

void OcrSettingsWidget::onOcrProviderChanged(int /*index*/)
{
    appConfig().settings.ocrProviderId =
        m_ui->ocrCombo->currentData().toString().toStdString();
    markConfigDirty();

    if (auto *vc = VoicePipelineController::instance()) {
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
    auto *controller = VoicePipelineController::instance();
    if (!controller) {
        STATUSBAR_INFO("{}", tr("OCR provider is not available."));
        return;
    }

    m_ui->resultEdit->clear();
    m_ui->copyResultButton->setEnabled(false);
    showPreview(image, {});
    m_ui->clipboardButton->setEnabled(false);
    m_ui->openImageButton->setEnabled(false);

    const QPointer<OcrSettingsWidget> guard(this);
    controller->submitDetailedOcr(
        image, [guard, image](const ApiOcrResult &result) {
            if (!guard) {
                return;
            }
            guard->m_ui->clipboardButton->setEnabled(true);
            guard->m_ui->openImageButton->setEnabled(true);
            if (!result.error.isEmpty()) {
                STATUSBAR_INFO("{}", result.error);
                return;
            }
            guard->m_ui->resultEdit->setPlainText(result.text);
            guard->m_ui->copyResultButton->setEnabled(!result.text.isEmpty());
            guard->showPreview(image, result.blocks);
        });
}

void OcrSettingsWidget::copyResult()
{
    const QString text = m_ui->resultEdit->toPlainText();
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
        m_ui->previewLabel->setPlaceholder(
            tr("OCR image preview will appear here"));
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

    QVector<OcrTextBlock> displayBlocks;
    displayBlocks.reserve(blocks.size());
    const QRectF imageRect(0, 0, displayImage.width(), displayImage.height());
    for (const auto &block : blocks) {
        QRectF bounds = block.bounds;
        bounds.setLeft(bounds.left() * scaleX);
        bounds.setTop(bounds.top() * scaleY);
        bounds.setWidth(bounds.width() * scaleX);
        bounds.setHeight(bounds.height() * scaleY);
        bounds = bounds.intersected(imageRect);
        if (!bounds.isEmpty() && !block.text.trimmed().isEmpty()) {
            displayBlocks.append({block.text, bounds});
        }
    }

    m_ui->previewLabel->setContent(displayImage, displayBlocks);
}

} // namespace zenny
