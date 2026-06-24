#pragma once

#include <QString>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QPropertyAnimation;
class ScrollTextDisplay;

namespace talkinput
{

class VoiceOverlay final : public QWidget
{
public:
    explicit VoiceOverlay(QWidget *parent = nullptr);

    void startAnimation();
    void stopAnimation();
    void stopBlinking();
    void setPreviewText(const QString &text);
    void setModeText(const QString &text);

private:
    void positionOnActiveScreen();

    QLabel *m_modeLabel = nullptr;
    QPropertyAnimation *m_blinkAnimation = nullptr;
    ScrollTextDisplay *m_scrollText = nullptr;
};

} // namespace talkinput
