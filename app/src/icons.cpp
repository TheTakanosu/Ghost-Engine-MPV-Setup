#include "icons.h"

#include <QPainter>
#include <QPainterPath>

namespace icons {

namespace {

// A triangle pointing right, its weight nudged left so it looks centred: a
// play mark's visual centre is left of its box's centre.
QPainterPath triangle(const QRectF &r)
{
    const qreal h = r.height(), w = h * 0.88;
    const qreal left = r.center().x() - w * 0.42;
    QPainterPath p;
    p.moveTo(left, r.top());
    p.lineTo(left + w, r.center().y());
    p.lineTo(left, r.bottom());
    p.closeSubpath();
    return p;
}

} // namespace

void paint(QPainter &painter, Icon icon, const QRectF &rect, const QColor &color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal s = qMin(rect.width(), rect.height());
    const QRectF r(rect.center().x() - s / 2, rect.center().y() - s / 2, s, s);
    const qreal line = qMax(1.6, s * 0.11);

    switch (icon) {
    case Icon::Play:
        painter.setPen(QPen(color, line * 0.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(color);
        painter.drawPath(triangle(r.adjusted(s * 0.12, s * 0.08, -s * 0.12, -s * 0.08)));
        break;
    case Icon::Pause: {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const qreal bar = s * 0.24, gap = s * 0.2, h = s * 0.82;
        const qreal top = r.center().y() - h / 2, mid = r.center().x();
        painter.drawRoundedRect(QRectF(mid - gap / 2 - bar, top, bar, h), bar * 0.3, bar * 0.3);
        painter.drawRoundedRect(QRectF(mid + gap / 2, top, bar, h), bar * 0.3, bar * 0.3);
        break;
    }
    case Icon::Previous:
    case Icon::Next: {
        painter.save();
        if (icon == Icon::Previous) {
            painter.translate(r.center());
            painter.scale(-1, 1);
            painter.translate(-r.center());
        }
        painter.setPen(QPen(color, line * 0.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(color);
        const QRectF t(r.left() + s * 0.14, r.top() + s * 0.18, s * 0.56, s * 0.64);
        painter.drawPath(triangle(t));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRectF(r.right() - s * 0.26, r.top() + s * 0.18, s * 0.12, s * 0.64), 1.5, 1.5);
        painter.restore();
        break;
    }
    case Icon::Shuffle: {
        painter.setPen(QPen(color, line, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const qreal l = r.left() + s * 0.1, rr = r.right() - s * 0.16;
        const qreal t = r.top() + s * 0.28, b = r.bottom() - s * 0.28;
        QPainterPath a, c;
        a.moveTo(l, t);
        a.cubicTo(r.center().x(), t, r.center().x(), b, rr, b);
        c.moveTo(l, b);
        c.cubicTo(r.center().x(), b, r.center().x(), t, rr, t);
        painter.drawPath(a);
        painter.drawPath(c);
        const qreal head = s * 0.14;
        for (qreal y : {t, b}) {
            painter.drawLine(QPointF(rr, y), QPointF(rr - head, y - head));
            painter.drawLine(QPointF(rr, y), QPointF(rr - head, y + head));
        }
        break;
    }
    case Icon::Folder: {
        painter.setPen(QPen(color, line, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QRectF f = r.adjusted(s * 0.1, s * 0.2, -s * 0.1, -s * 0.16);
        QPainterPath p;
        p.moveTo(f.left(), f.bottom());
        p.lineTo(f.left(), f.top());
        p.lineTo(f.left() + f.width() * 0.36, f.top());
        p.lineTo(f.left() + f.width() * 0.46, f.top() + f.height() * 0.18);
        p.lineTo(f.right(), f.top() + f.height() * 0.18);
        p.lineTo(f.right(), f.bottom());
        p.closeSubpath();
        painter.drawPath(p);
        break;
    }
    case Icon::Repeat:
    case Icon::RepeatOne: {
        // A loop with a gap at the top right and an arrowhead closing it.
        painter.setPen(QPen(color, line, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QRectF loop = r.adjusted(s * 0.1, s * 0.24, -s * 0.1, -s * 0.24);
        const qreal round = loop.height() * 0.45;
        QPainterPath p;
        p.moveTo(loop.right() - s * 0.18, loop.top());
        p.lineTo(loop.left() + round, loop.top());
        p.arcTo(QRectF(loop.left(), loop.top(), 2 * round, 2 * round), 90, 90);
        p.lineTo(loop.left(), loop.bottom() - round);
        p.arcTo(QRectF(loop.left(), loop.bottom() - 2 * round, 2 * round, 2 * round), 180, 90);
        p.lineTo(loop.right() - round, loop.bottom());
        p.arcTo(QRectF(loop.right() - 2 * round, loop.bottom() - 2 * round, 2 * round, 2 * round), 270, 90);
        p.lineTo(loop.right(), loop.top() + round);
        painter.drawPath(p);
        const qreal head = s * 0.13;
        const QPointF tip(loop.right() - s * 0.18, loop.top());
        painter.drawLine(tip, tip + QPointF(-head, -head));
        painter.drawLine(tip, tip + QPointF(-head, head));
        if (icon == Icon::RepeatOne) {
            // A filled badge on the loop's top right with the "1" cut out of
            // it in the bar's dark, big enough to read at the bar's size.
            const qreal d = s * 0.64;
            const QRectF badge(r.right() - d * 0.92, r.top() - d * 0.12, d, d);
            painter.setPen(QPen(QColor(12, 11, 15), s * 0.08));
            painter.setBrush(color);
            painter.drawEllipse(badge);
            QFont f = painter.font();
            f.setPixelSize(qMax(8, int(d * 0.82)));
            f.setWeight(QFont::Black);
            painter.setFont(f);
            painter.setPen(QColor(12, 11, 15));
            painter.drawText(badge.adjusted(0, -d * 0.04, 0, 0), Qt::AlignCenter, QStringLiteral("1"));
        }
        break;
    }
    case Icon::Back10:
    case Icon::Forward10: {
        // A circling arrow with "10" inside: a jump of ten seconds, not a
        // step to the next video (which the plain triangles are).
        painter.save();
        if (icon == Icon::Back10) {
            painter.translate(r.center());
            painter.scale(-1, 1);
            painter.translate(-r.center());
        }
        painter.setPen(QPen(color, line * 0.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const qreal rad = s * 0.42;
        const QRectF ring(r.center().x() - rad, r.center().y() - rad, 2 * rad, 2 * rad);
        QPainterPath arc;
        arc.arcMoveTo(ring, 90);
        arc.arcTo(ring, 90, 290);
        painter.drawPath(arc);
        const qreal head = s * 0.13;
        const QPointF tip(r.center().x() + head * 0.7, ring.top());
        painter.drawLine(tip, tip + QPointF(-head, -head));
        painter.drawLine(tip, tip + QPointF(-head, head));
        painter.drawLine(QPointF(ring.center().x(), ring.top()), tip);
        painter.restore();
        QFont f = painter.font();
        f.setPixelSize(qMax(7, int(s * 0.34)));
        f.setWeight(QFont::Bold);
        painter.setFont(f);
        painter.setPen(color);
        painter.drawText(r.adjusted(0, s * 0.04, 0, 0), Qt::AlignCenter, QStringLiteral("10"));
        break;
    }
    case Icon::Clock: {
        painter.setPen(QPen(color, line * 0.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QRectF face = r.adjusted(s * 0.08, s * 0.08, -s * 0.08, -s * 0.08);
        painter.drawEllipse(face);
        painter.drawLine(face.center(), QPointF(face.center().x(), face.top() + face.height() * 0.24));
        painter.drawLine(face.center(), QPointF(face.center().x() + face.width() * 0.2, face.center().y() + face.height() * 0.1));
        break;
    }
    case Icon::Playing: {
        // Three bars of different heights: the "now playing" equaliser mark.
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const qreal bar = s * 0.18, gap = s * 0.1;
        const qreal heights[] = {0.55, 0.9, 0.7};
        const qreal left = r.center().x() - (3 * bar + 2 * gap) / 2;
        for (int i = 0; i < 3; ++i) {
            const qreal h = s * 0.8 * heights[i];
            painter.drawRoundedRect(QRectF(left + i * (bar + gap), r.bottom() - s * 0.1 - h, bar, h), 1, 1);
        }
        break;
    }
    }
    painter.restore();
}

IconButton::IconButton(Icon icon, int size, const QString &tip, QWidget *parent)
    : QAbstractButton(parent), m_icon(icon), m_size(size)
{
    setToolTip(tip);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(size, size);
    setAttribute(Qt::WA_Hover);
}

void IconButton::setRound(const QColor &fill, const QColor &fillHover, const QColor &glyph)
{
    m_round = true;
    m_fill = fill;
    m_fillHover = fillHover;
    m_normal = m_hover = glyph;
    update();
}

void IconButton::setColors(const QColor &normal, const QColor &hover)
{
    m_normal = normal;
    m_hover = hover;
    update();
}

void IconButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool hot = underMouse();
    const qreal scale = isDown() ? 0.94 : 1.0;
    const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
    if (m_round) {
        p.setPen(Qt::NoPen);
        p.setBrush(hot ? m_fillHover : m_fill);
        const qreal d = box.width() * scale;
        p.drawEllipse(QRectF(box.center().x() - d / 2, box.center().y() - d / 2, d, d));
    }
    const qreal glyph = (m_round ? 0.4 : 0.62) * box.width() * scale;
    // With a dot, the icon moves up a little to make room under it.
    const qreal lift = m_dot ? box.height() * 0.1 : 0;
    paint(p, m_icon, QRectF(box.center().x() - glyph / 2, box.center().y() - glyph / 2 - lift, glyph, glyph),
          hot ? m_hover : m_normal);
    if (m_dot) {
        p.setPen(Qt::NoPen);
        p.setBrush(m_normal);
        const qreal d = qMax(3.0, box.width() * 0.13);
        p.drawEllipse(QRectF(box.center().x() - d / 2, box.bottom() - d - 0.5, d, d));
    }
}

} // namespace icons
