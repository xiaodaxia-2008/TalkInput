#pragma once

#include <QString>
#include <QWidget>
#include <memory>

namespace Ui
{
class VoiceOverlay;
}

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
    ~VoiceOverlay() override;

    void startAnimation();
    void stopAnimation();
    void stopBlinking();
    void setPreviewText(const QString &text);
    void setModeText(const QString &text);

private:
    void positionOnActiveScreen();

    std::unique_ptr<Ui::VoiceOverlay> m_ui;
    QPropertyAnimation *m_blinkAnimation = nullptr;
};

} // namespace talkinput
