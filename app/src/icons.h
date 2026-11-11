#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QRectF>

class QPainter;

// Player icons drawn as shapes rather than font glyphs: icon fonts sit off
// centre and thin out at small sizes, and these are centred to the pixel.
namespace icons {

enum class Icon { Play, Pause, Previous, Next, Shuffle, Folder, Playing, Clock, Repeat, RepeatOne, Back10, Forward10 };

void paint(QPainter &painter, Icon icon, const QRectF &rect, const QColor &color);

// The Ghost Engine ghost (the app's icon, assets/make_icon.py): its body in
// `body` over an offset shadow in `shadow`, the eyes cut through, filling
// the square `rect`.
void paintGhost(QPainter &painter, const QRectF &rect, const QColor &body, const QColor &shadow);

// The seconds Back10/Forward10 jump and show inside their arrow
// (Settings > Playback > Seek step; 10 until set).
int skipSeconds();
void setSkipSeconds(int seconds);

// A flat button showing one of the icons. `round` gives it a filled circle
// behind the icon, as the play button has.
class IconButton : public QAbstractButton {
public:
    IconButton(Icon icon, int size, const QString &tip, QWidget *parent = nullptr);

    void setIcon(Icon icon) { m_icon = icon; update(); }
    void setRound(const QColor &fill, const QColor &fillHover, const QColor &glyph);
    void setColors(const QColor &normal, const QColor &hover);
    // A small dot under the icon, as Spotify marks shuffle and repeat that are on.
    void setDot(bool dot) { m_dot = dot; update(); }

    QSize sizeHint() const override { return {m_size, m_size}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    Icon m_icon;
    int m_size;
    bool m_round = false;
    bool m_dot = false;
    QColor m_fill, m_fillHover, m_normal, m_hover;   // invalid: the theme's icon colours
};

} // namespace icons
