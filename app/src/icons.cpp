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
    paint(p, m_icon, QRectF(box.center().x() - glyph / 2, box.center().y() - glyph / 2, glyph, glyph),
          hot ? m_hover : m_normal);
}

} // namespace icons
