#include "voice_overlay.h"
#include "scroll_text_display.h"

#include <QCursor>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QScreen>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace
{

#ifdef Q_OS_WIN
static void enableAcrylic(HWND hwnd)
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (!hUser) {
        return;
    }

    using SWCA = BOOL(WINAPI *)(HWND, void *);
    auto func = reinterpret_cast<SWCA>(
        GetProcAddress(hUser, "SetWindowCompositionAttribute"));
    if (!func) {
        return;
    }

    struct AccentPolicy
    {
        DWORD state;
        DWORD flags;
        DWORD color;
        DWORD animId;
    };

    struct WinCompAttrData
    {
        DWORD attr;
        const void *data;
        DWORD dataSize;
    };

    AccentPolicy accent = {4, 0, 0xC0101012, 0};
    WinCompAttrData wcad = {19, &accent, sizeof(accent)};
    func(hwnd, &wcad);
}

void applyNativeOverlayEffects(QWidget *widget)
{
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    enableAcrylic(hwnd);
}
#else
void applyNativeOverlayEffects(QWidget *)
{
}
#endif

} // namespace

namespace talkinput
{

VoiceOverlay::VoiceOverlay(QWidget *parent) : QWidget(parent)
{
    setObjectName("voiceOverlay");
    setWindowTitle(QStringLiteral("TalkInput"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedHeight(72);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 6, 14, 6);
    layout->setSpacing(8);

    auto *micLabel = new QLabel(QStringLiteral("\xF0\x9F\x8E\x99"), this);
    micLabel->setObjectName("voiceOverlayMicLabel");
    layout->addWidget(micLabel);

    auto *effect = new QGraphicsOpacityEffect(micLabel);
    micLabel->setGraphicsEffect(effect);
    m_blinkAnimation = new QPropertyAnimation(effect, "opacity", this);
    m_blinkAnimation->setDuration(1200);
    m_blinkAnimation->setStartValue(1.0);
    m_blinkAnimation->setEndValue(0.15);
    m_blinkAnimation->setLoopCount(-1);
    m_blinkAnimation->setEasingCurve(QEasingCurve::InOutSine);

    m_scrollText = new ScrollTextDisplay(this);
    layout->addWidget(m_scrollText, 1);

    setMinimumWidth(320);
}

void VoiceOverlay::startAnimation()
{
    m_blinkAnimation->start();
    m_scrollText->setText(QString());
    show();
    raise();
    positionOnActiveScreen();
    applyNativeOverlayEffects(this);
}

void VoiceOverlay::stopAnimation()
{
    m_blinkAnimation->stop();
    static_cast<QGraphicsOpacityEffect *>(m_blinkAnimation->targetObject())
        ->setOpacity(1.0);
    hide();
}

void VoiceOverlay::setPreviewText(const QString &text)
{
    m_scrollText->setText(text);
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
