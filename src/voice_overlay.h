#pragma once

#include <QString>
#include <QWidget>

class QGraphicsOpacityEffect;
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
    void setPreviewText(const QString &text);

private:
    void positionOnActiveScreen();

    QPropertyAnimation *m_blinkAnimation = nullptr;
    ScrollTextDisplay *m_scrollText = nullptr;
};

} // namespace talkinput
