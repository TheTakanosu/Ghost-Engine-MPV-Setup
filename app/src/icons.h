#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QRectF>

class QPainter;

// Player icons drawn as shapes rather than font glyphs: icon fonts sit off
// centre and thin out at small sizes, and these are centred to the pixel.
namespace icons {

enum class Icon { Play, Pause, Previous, Next, Shuffle, Folder, Playing, Clock, Repeat, RepeatOne };

void paint(QPainter &painter, Icon icon, const QRectF &rect, const QColor &color);

// A flat button showing one of the icons. `round` gives it a filled circle
// behind the icon, as the play button has.
class IconButton : public QAbstractButton {
public:
    IconButton(Icon icon, int size, const QString &tip, QWidget *parent = nullptr);

    void setIcon(Icon icon) { m_icon = icon; update(); }
    void setRound(const QColor &fill, const QColor &fillHover, const QColor &glyph);
    void setColors(const QColor &normal, const QColor &hover);

    QSize sizeHint() const override { return {m_size, m_size}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    Icon m_icon;
    int m_size;
    bool m_round = false;
    QColor m_fill, m_fillHover, m_normal{170, 168, 182}, m_hover{Qt::white};
};

} // namespace icons
