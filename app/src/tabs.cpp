#include "tabs.h"

#include "theme.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

constexpr int kMaxWidth = 210;   // a tab's, with room to spare
constexpr int kMinWidth = 46;    // only its cover
constexpr int kGap = 6;
constexpr int kHeight = 36;

int z(int v) { return qRound(v * theme::chromeZoom()); }

} // namespace

TabStrip::TabStrip(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(z(kHeight));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    theme::whenChanged(this, [this] { setFixedHeight(z(kHeight)); update(); });
}

void TabStrip::changed()
{
    if (current >= tabs.size())
        current = -1;
    m_hover = -1;
    setMaximumWidth(qMax(1, int(tabs.size()) * (z(kMaxWidth) + z(kGap))));
    updateGeometry();
    update();
}

QSize TabStrip::sizeHint() const
{
    return QSize(int(tabs.size()) * (z(kMaxWidth) + z(kGap)), z(kHeight));
}

QSize TabStrip::minimumSizeHint() const
{
    return QSize(int(tabs.size()) * (z(kMinWidth) + z(kGap)), z(kHeight));
}

int TabStrip::tabWidth() const
{
    if (tabs.isEmpty())
        return 0;
    const int each = (width() - int(tabs.size() - 1) * z(kGap)) / int(tabs.size());
    return qBound(z(kMinWidth), each, z(kMaxWidth));
}

QRect TabStrip::tabRect(int i) const
{
    const int w = tabWidth();
    return QRect(i * (w + z(kGap)), 0, w, height());
}

QRect TabStrip::closeRect(int i) const
{
    const QRect r = tabRect(i);
    if (r.width() < z(110))
        return QRect();   // narrow: closed with the middle button or the menu
    const int s = z(20);
    return QRect(r.right() - s - z(6), r.center().y() - s / 2 + 1, s, s);
}

int TabStrip::tabAt(const QPoint &p) const
{
    for (int i = 0; i < tabs.size(); ++i)
        if (tabRect(i).contains(p))
            return i;
    return -1;
}

void TabStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const theme::Palette &c = theme::c();
    QFont f = font();
    f.setPixelSize(z(12));
    for (int i = 0; i < tabs.size(); ++i) {
        const VideoTab &t = tabs[i];
        const QRect r = tabRect(i);
        const bool now = i == current;
        const bool hover = i == m_hover;
        QPainterPath box;
        box.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), z(10), z(10));
        p.fillPath(box, now ? c.cardSelected : hover ? c.raised : c.card);
        if (now) {
            p.setPen(QPen(c.accent, 1.2));
            p.drawPath(box);
        }

        // The cover, 16:9, at the start.
        const int th = r.height() - z(10);
        const QRect thumb(r.left() + z(5), r.top() + z(5), th * 16 / 9, th);
        QPainterPath round;
        round.addRoundedRect(QRectF(thumb), z(5), z(5));
        const QPixmap pic = cover && !t.id.isEmpty() ? cover(t.id) : QPixmap();
        if (pic.isNull()) {
            p.fillPath(round, c.track);
        } else {
            p.save();
            p.setClipPath(round);
            const QSize scaled = pic.size().scaled(thumb.size(), Qt::KeepAspectRatioByExpanding);
            p.drawPixmap(QRect(thumb.center() - QPoint(scaled.width() / 2, scaled.height() / 2), scaled), pic);
            p.restore();
        }
        if (now) {
            // What plays: a small play mark over its cover.
            p.fillPath(round, QColor(0, 0, 0, 110));
            QFont g(QStringLiteral("Segoe Fluent Icons"));
            g.setPixelSize(z(11));
            p.setFont(g);
            p.setPen(Qt::white);
            p.drawText(thumb, Qt::AlignCenter, QString(QChar(0xE768)));
        }

        // The name, then the close mark on the one under the mouse.
        const QRect close = closeRect(i);
        const int textLeft = thumb.right() + z(8);
        const int textRight = (hover || now) && !close.isNull() ? close.left() - z(4) : r.right() - z(10);
        if (textRight - textLeft > z(16)) {
            p.setFont(f);
            p.setPen(now ? c.strong : c.text);
            const QRect tr(textLeft, r.top(), textRight - textLeft, r.height());
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetrics(f).elidedText(t.title.isEmpty() ? t.target : t.title, Qt::ElideRight, tr.width()));
        }
        if ((hover || now) && !close.isNull()) {
            if (hover && m_hoverClose) {
                QPainterPath dot;
                dot.addEllipse(QRectF(close));
                p.fillPath(dot, c.raisedHover);
            }
            QFont g(QStringLiteral("Segoe Fluent Icons"));
            g.setPixelSize(z(9));
            p.setFont(g);
            p.setPen(hover && m_hoverClose ? c.strong : c.dim);
            p.drawText(close, Qt::AlignCenter, QString(QChar(0xE8BB)));
        }
    }
}

void TabStrip::mouseMoveEvent(QMouseEvent *event)
{
    const int at = tabAt(event->position().toPoint());
    const bool onClose = at >= 0 && closeRect(at).contains(event->position().toPoint());
    if (at != m_hover || onClose != m_hoverClose) {
        m_hover = at;
        m_hoverClose = onClose;
        if (at >= 0) {
            const VideoTab &t = tabs[at];
            setToolTip(t.channel.isEmpty() ? t.title : QStringLiteral("%1\n%2").arg(t.title, t.channel));
        } else {
            setToolTip(QString());
        }
        update();
    }
}

void TabStrip::mousePressEvent(QMouseEvent *event)
{
    m_pressed = tabAt(event->position().toPoint());
    event->accept();
}

void TabStrip::mouseReleaseEvent(QMouseEvent *event)
{
    const QPoint at = event->position().toPoint();
    const int i = tabAt(at);
    const int pressed = m_pressed;
    m_pressed = -1;
    if (i < 0 || i != pressed)
        return;
    if (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && closeRect(i).contains(at))) {
        if (onClose)
            onClose(i);
    } else if (event->button() == Qt::LeftButton && onPick) {
        onPick(i);
    }
}

void TabStrip::leaveEvent(QEvent *)
{
    m_hover = -1;
    m_hoverClose = false;
    update();
}

void TabStrip::contextMenuEvent(QContextMenuEvent *event)
{
    const int i = tabAt(event->pos());
    if (i >= 0 && onMenu)
        onMenu(i, event->globalPos());
}
