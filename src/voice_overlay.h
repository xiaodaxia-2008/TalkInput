#pragma once

#include <QString>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QPropertyAnimation;
class ScrollTextDisplay;

namespace zenny
{

class VoiceOverlay final : public QWidget
{
public:
    explicit VoiceOverlay(QWidget *parent = nullptr);
    ~VoiceOverlay() override;

    void startAnimation();
    void stopAnimation();
    void stopBlinking();
    void setPreviewText(const QString &text);
    void setModeText(const QString &text);

private:
    void positionOnActiveScreen();

    QLabel *m_modeLabel = nullptr;
    ScrollTextDisplay *m_scrollText = nullptr;
    QPropertyAnimation *m_blinkAnimation = nullptr;
};

} // namespace zenny
