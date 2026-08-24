#include "voice_overlay.h"
#include "app_config.h"
#include "scroll_text_display.h"
#include "ui_voice_overlay.h"

#include <QCursor>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QScreen>

namespace talkinput
{

VoiceOverlay::VoiceOverlay(QWidget *parent) : QWidget(parent)
{
    setObjectName("voiceOverlay");
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setWindowOpacity(appConfig().settings.overlayOpacity);
    setFixedHeight(72);

    m_ui = std::make_unique<Ui::VoiceOverlay>();
    m_ui->setupUi(this);

    auto *effect = new QGraphicsOpacityEffect(m_ui->modeLabel);
    m_ui->modeLabel->setGraphicsEffect(effect);
    m_blinkAnimation = new QPropertyAnimation(effect, "opacity", this);
    m_blinkAnimation->setDuration(1200);
    m_blinkAnimation->setStartValue(1.0);
    m_blinkAnimation->setEndValue(0.15);
    m_blinkAnimation->setLoopCount(-1);
    m_blinkAnimation->setEasingCurve(QEasingCurve::InOutSine);

    setMinimumWidth(320);
}

VoiceOverlay::~VoiceOverlay() = default;

void VoiceOverlay::startAnimation()
{
    m_blinkAnimation->start();
    show();
    raise();
    positionOnActiveScreen();
}

void VoiceOverlay::stopAnimation()
{
    stopBlinking();
    hide();
}

void VoiceOverlay::stopBlinking()
{
    m_blinkAnimation->stop();
    static_cast<QGraphicsOpacityEffect *>(m_blinkAnimation->targetObject())
        ->setOpacity(1.0);
}

void VoiceOverlay::setPreviewText(const QString &text)
{
    m_ui->scrollText->setText(text);
}

void VoiceOverlay::setModeText(const QString &text)
{
    m_ui->modeLabel->setText(text);
}

void VoiceOverlay::positionOnActiveScreen()
{
    QPoint cursorPos = QCursor::pos();
    QScreen *screen = QGuiApplication::screenAt(cursorPos);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    QRect workArea = screen->availableGeometry();
    int x = workArea.left() + workArea.width() / 2 - width() / 2;
    if (x < workArea.left()) {
        x = workArea.left() + 8;
    }
    move(x, workArea.bottom() - height() - 30);
}

} // namespace talkinput
