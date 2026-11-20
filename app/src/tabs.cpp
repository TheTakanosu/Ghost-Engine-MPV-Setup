#include "tabs.h"

#include "theme.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

constexpr int kMaxWidth = 210;   // a tab in the strip, with room to spare
constexpr int kMinWidth = 132;   // its cover, a few words and the close mark
constexpr int kMoreWidth = 52;   // the "+N" chip
constexpr int kGap = 6;
constexpr int kHeight = 36;
constexpr int kRow = 58;         // a row of the list
constexpr int kListWidth = 380;

int z(int v) { return qRound(v * theme::chromeZoom()); }

QFont icons(int px)
{
    QFont g(QStringLiteral("Segoe Fluent Icons"));
    g.setPixelSize(px);
    return g;
}

} // namespace

TabStrip::TabStrip(bool vertical, QWidget *parent) : QWidget(parent), m_vertical(vertical)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    if (vertical) {
        setFixedWidth(z(kListWidth));
    } else {
        setFixedHeight(z(kHeight));
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }
    theme::whenChanged(this, [this] { changed(); });
}

void TabStrip::changed()
{
    if (current >= tabs.size())
        current = -1;
    m_hover = -1;
    if (m_vertical) {
        setFixedWidth(z(kListWidth));
        setFixedHeight(qMax(1, int(tabs.size())) * z(kRow) + z(8));
    } else {
        setFixedHeight(z(kHeight));
    }
    updateGeometry();
    lay();
    update();
}

int TabStrip::smallest() const
{
    return tabs.size() <= 1 ? z(kMinWidth) : z(kMinWidth) + z(kGap) + z(kMoreWidth);
}

QSize TabStrip::sizeHint() const
{
    if (m_vertical)
        return QSize(z(kListWidth), int(tabs.size()) * z(kRow) + z(8));
    return QSize(int(tabs.size()) * (z(kMaxWidth) + z(kGap)), z(kHeight));
}

QSize TabStrip::minimumSizeHint() const
{
    if (m_vertical)
        return sizeHint();
    return QSize(smallest(), z(kHeight));
}

void TabStrip::resizeEvent(QResizeEvent *)
{
    lay();
}

void TabStrip::lay()
{
    m_shown.clear();
    m_more = QRect();
    const int n = int(tabs.size());
    if (n == 0)
        return;
    if (m_vertical) {
        for (int i = 0; i < n; ++i)
            m_shown.append({i, QRect(z(4), z(4) + i * z(kRow), width() - z(8), z(kRow) - z(4))});
        return;
    }
    const int gap = z(kGap);
    int fit = (width() + gap) / (z(kMinWidth) + gap);
    QVector<int> which;
    if (fit >= n) {
        for (int i = 0; i < n; ++i)
            which << i;
    } else {
        // As many as fit beside the chip; what plays always among them.
        fit = qMax(1, (width() - z(kMoreWidth)) / (z(kMinWidth) + gap));
        for (int i = 0; i < n && which.size() < fit; ++i)
            which << i;
        if (current >= fit) {
            which.removeLast();
            which << current;
        }
    }
    const int room = which.size() < n ? width() - z(kMoreWidth) - gap : width();
    const int w = qBound(z(kMinWidth), (room - (int(which.size()) - 1) * gap) / int(which.size()), z(kMaxWidth));
    int x = 0;
    for (int i : std::as_const(which)) {
        m_shown.append({i, QRect(x, 0, w, height())});
        x += w + gap;
    }
    if (which.size() < n)
        m_more = QRect(x, 0, z(kMoreWidth), height());
}

QRect TabStrip::closeRect(const QRect &r) const
{
    const int s = z(20);
    return QRect(r.right() - s - z(6), r.center().y() - s / 2 + 1, s, s);
}

int TabStrip::tabAt(const QPoint &p) const
{
    for (const auto &[i, r] : m_shown)
        if (r.contains(p))
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
    for (const auto &[i, r] : std::as_const(m_shown)) {
        const VideoTab &t = tabs[i];
        const bool now = i == current;
        const bool hover = i == m_hover;
        QPainterPath box;
        box.addRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), z(10), z(10));
        const QColor rest = m_vertical ? QColor(Qt::transparent) : c.card;
        p.fillPath(box, now ? c.cardSelected : hover ? c.raised : rest);
        if (now && !m_vertical) {
            p.setPen(QPen(c.accent, 1.2));
            p.drawPath(box);
        }
        p.save();
        p.setClipPath(box);   // nothing past the tab's edge (a cover ran over, 2026-11-20)

        // The cover, 16:9, at the start.
        const int th = m_vertical ? r.height() - z(10) : r.height() - z(10);
        const QRect thumb(r.left() + z(5), r.top() + (r.height() - th) / 2, th * 16 / 9, th);
        QPainterPath round;
        round.addRoundedRect(QRectF(thumb), z(5), z(5));
        const QPixmap pic = cover && !t.id.isEmpty() ? cover(t.id) : QPixmap();
        if (pic.isNull()) {
            p.fillPath(round, c.track);
        } else {
            p.save();
            p.setClipPath(round, Qt::IntersectClip);
            const QSize scaled = pic.size().scaled(thumb.size(), Qt::KeepAspectRatioByExpanding);
            p.drawPixmap(QRect(thumb.center() - QPoint(scaled.width() / 2, scaled.height() / 2), scaled), pic);
            p.restore();
        }
        if (now) {
            // What plays: a small play mark over its cover.
            p.fillPath(round, QColor(0, 0, 0, 110));
            p.setFont(icons(z(11)));
            p.setPen(Qt::white);
            p.drawText(thumb, Qt::AlignCenter, QString(QChar(0xE768)));
        }

        // The name (and in the list the channel under it), then the close
        // mark on the one under the mouse.
        const QRect close = closeRect(r);
        const bool showClose = hover || (now && !m_vertical);
        const int textLeft = thumb.right() + z(10);
        const int textRight = showClose ? close.left() - z(4) : r.right() - z(10);
        const QString name = t.title.isEmpty() ? t.target : t.title;
        if (textRight - textLeft > z(16)) {
            const int tw = textRight - textLeft;
            p.setFont(f);
            p.setPen(now ? c.strong : c.text);
            if (m_vertical) {
                const QFontMetrics fm(f);
                p.drawText(QRect(textLeft, r.top() + z(8), tw, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                           fm.elidedText(name, Qt::ElideRight, tw));
                QFont small = f;
                small.setPixelSize(z(11));
                p.setFont(small);
                p.setPen(c.muted);
                const QString under = t.seconds >= 5
                    ? QStringLiteral("%1%2%3:%4").arg(t.channel, t.channel.isEmpty() ? QString() : QStringLiteral("  ·  "))
                          .arg(int(t.seconds) / 60).arg(int(t.seconds) % 60, 2, 10, QChar('0'))
                    : t.channel;
                p.drawText(QRect(textLeft, r.bottom() - z(8) - QFontMetrics(small).height(), tw, QFontMetrics(small).height()),
                           Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(small).elidedText(under, Qt::ElideRight, tw));
            } else {
                p.drawText(QRect(textLeft, r.top(), tw, r.height()), Qt::AlignVCenter | Qt::AlignLeft,
                           QFontMetrics(f).elidedText(name, Qt::ElideRight, tw));
            }
        }
        if (showClose) {
            if (hover && m_hoverClose) {
                QPainterPath dot;
                dot.addEllipse(QRectF(close));
                p.fillPath(dot, c.raisedHover);
            }
            p.setFont(icons(z(9)));
            p.setPen(hover && m_hoverClose ? c.strong : c.dim);
            p.drawText(close, Qt::AlignCenter, QString(QChar(0xE8BB)));
        }
        p.restore();
    }
    if (!m_more.isNull()) {
        QPainterPath chip;
        chip.addRoundedRect(QRectF(m_more).adjusted(0.5, 0.5, -0.5, -0.5), z(10), z(10));
        p.fillPath(chip, m_hoverMore ? c.raised : c.card);
        p.setFont(f);
        p.setPen(c.text);
        int hidden = int(tabs.size()) - int(m_shown.size());
        p.drawText(m_more, Qt::AlignCenter, QStringLiteral("+%1").arg(hidden));
    }
}

void TabStrip::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint at = event->position().toPoint();
    const int i = tabAt(at);
    bool onClose = false;
    for (const auto &[j, r] : std::as_const(m_shown))
        if (j == i)
            onClose = closeRect(r).contains(at);
    const bool onMore = m_more.contains(at);
    if (i != m_hover || onClose != m_hoverClose || onMore != m_hoverMore) {
        m_hover = i;
        m_hoverClose = onClose;
        m_hoverMore = onMore;
        if (onMore) {
            setToolTip(QStringLiteral("All tabs"));
        } else if (i >= 0) {
            const VideoTab &t = tabs[i];
            setToolTip(onClose ? QStringLiteral("Close tab")
                       : t.channel.isEmpty() ? t.title : QStringLiteral("%1\n%2").arg(t.title, t.channel));
        } else {
            setToolTip(QString());
        }
        update();
    }
}

void TabStrip::mousePressEvent(QMouseEvent *event)
{
    m_pressed = tabAt(event->position().toPoint());
    m_pressedMore = m_more.contains(event->position().toPoint());
    event->accept();
}

void TabStrip::mouseReleaseEvent(QMouseEvent *event)
{
    const QPoint at = event->position().toPoint();
    if (m_pressedMore && m_more.contains(at) && event->button() == Qt::LeftButton) {
        m_pressedMore = false;
        if (onMore)
            onMore(mapToGlobal(QPoint(m_more.right(), m_more.bottom() + z(6))));
        return;
    }
    m_pressedMore = false;
    const int i = tabAt(at);
    const int pressed = m_pressed;
    m_pressed = -1;
    if (i < 0 || i != pressed)
        return;
    QRect r;
    for (const auto &[j, place] : std::as_const(m_shown))
        if (j == i)
            r = place;
    const bool closeShown = i == m_hover || (i == current && !m_vertical);
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && closeShown && closeRect(r).contains(at))) {
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
    m_hoverMore = false;
    update();
}

void TabStrip::contextMenuEvent(QContextMenuEvent *event)
{
    const int i = tabAt(event->pos());
    if (i >= 0 && onMenu)
        onMenu(i, event->globalPos());
}
