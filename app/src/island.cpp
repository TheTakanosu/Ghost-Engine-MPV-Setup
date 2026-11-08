#include "island.h"

#include "icons.h"
#include "mpvclient.h"
#include "theme.h"
#include "thumbnails.h"
#include "youtube.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVariantAnimation>

namespace {

constexpr int kMargin = 12;       // room round the body for its shadow
constexpr int kTop = 9;           // the pill sits in the 50 px top bar
constexpr int kPillHeight = 32;
constexpr int kCardWidth = 460;
constexpr int kCardHeight = 150;
constexpr int kThumbW = 192, kThumbH = 108;

QColor mixed(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

} // namespace

NowIsland::NowIsland(MpvClient *mpv, ThumbnailCache *covers, QWidget *parent)
    : QWidget(parent), m_mpv(mpv), m_covers(covers)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    hide();

    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(200);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_open = v.toReal();
        place();
        update();
    });
    // The pointer has to rest a moment before it opens, so a quick click on
    // the pill goes straight to the video; leaving closes it after a beat.
    m_hover = new QTimer(this);
    m_hover->setSingleShot(true);
    connect(m_hover, &QTimer::timeout, this, [this] { setOpen(m_wantOpen); });

    const auto textButton = [this](const QString &text, const QString &tip) {
        auto *b = new QPushButton(text, this);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        theme::style(b, QStringLiteral("QPushButton { background: @raised; color: @textBright; border: none; border-radius: 13px;"
                                       " padding: 4px 12px; font-weight: 600; }"
                                       "QPushButton:hover { background: @raisedHover; }"));
        b->hide();
        return b;
    };
    m_play = new icons::IconButton(icons::Icon::Pause, 30, "Play / pause", this);
    m_play->hide();
    connect(m_play, &QAbstractButton::clicked, this, [this] { m_mpv->command({"cycle", "pause"}); });
    m_openButton = textButton("Open", "Go to the video's page");
    connect(m_openButton, &QPushButton::clicked, this, [this] { if (onOpen) onOpen(); });
    m_miniButton = textButton("Mini player", "Keep watching in a small window in the corner");
    connect(m_miniButton, &QPushButton::clicked, this, [this] {
        setOpen(false);
        if (onMini)
            onMini();
    });
    theme::whenChanged(this, [this] {
        m_play->setColors(theme::c().textBright, theme::accent());
        update();
    });
    m_play->setColors(theme::c().textBright, theme::accent());

    connect(m_mpv, &MpvClient::propertyChanged, this, [this](const QString &name) {
        if (name == "media-title" || name == "metadata/by-key/uploader" || name == "user-data/ghost/source" ||
            name == "path" || name == "user-data/ghost/audio")
            refresh();
        if (name == "pause")
            m_play->setIcon(m_mpv->property("pause").toBool() ? icons::Icon::Play : icons::Icon::Pause);
    });
    connect(m_mpv, &MpvClient::connectionChanged, this, [this] { refresh(); });
    connect(m_covers, &ThumbnailCache::ready, this, [this](const QString &id) {
        if (id == m_videoId)
            update();
    });
    parent->installEventFilter(this);
}

void NowIsland::refresh()
{
    const bool wanted = m_mpv->connected() && shouldShow && shouldShow();
    if (!wanted) {
        m_anim->stop();
        m_open = 0;
        m_wantOpen = false;
        hide();
        return;
    }
    m_title = m_mpv->property("media-title").toString();
    m_channel = m_mpv->property("metadata/by-key/uploader").toString();
    QString id = youtube::videoId(m_mpv->property("user-data/ghost/source").toString());
    if (id.isEmpty())
        id = youtube::videoId(m_mpv->property("path").toString());
    m_videoId = id;
    m_play->setIcon(m_mpv->property("pause").toBool() ? icons::Icon::Play : icons::Icon::Pause);
    place();
    show();
    raise();
    update();
}

QRectF NowIsland::body() const
{
    return QRectF(kMargin, kTop, width() - 2 * kMargin, height() - kTop - kMargin);
}

void NowIsland::place()
{
    QWidget *host = parentWidget();
    if (!host)
        return;
    // The pill as wide as its words (within reason), the card a set size.
    QFont f = font();
    f.setWeight(QFont::DemiBold);
    const int words = QFontMetrics(f).horizontalAdvance(QStringLiteral("Now playing  ·  ") + m_title);
    const int pillW = qBound(220, words + 56, 420);
    const qreal t = m_open;
    const int w = int(pillW + (kCardWidth - pillW) * t) + 2 * kMargin;
    const int h = int(kPillHeight + (kCardHeight - kPillHeight) * t) + kTop + kMargin;
    setGeometry((host->width() - w) / 2, 0, w, h);

    // The buttons only once the card is mostly open.
    const bool shown = t > 0.85;
    const QRectF b = body();
    const int y = int(b.bottom()) - 40;
    const int x = int(b.left()) + 12 + kThumbW + 14;
    m_play->move(x, y + 2);
    m_openButton->adjustSize();
    m_miniButton->adjustSize();
    m_openButton->move(x + 40, y + 4);
    m_miniButton->move(x + 40 + m_openButton->width() + 8, y + 4);
    for (QWidget *button : {static_cast<QWidget *>(m_play), static_cast<QWidget *>(m_openButton),
                            static_cast<QWidget *>(m_miniButton)})
        button->setVisible(shown);
}

bool NowIsland::eventFilter(QObject *watched, QEvent *event)
{
    // The window resized: still in the middle of the top bar.
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
        place();
    return false;
}

void NowIsland::setOpen(bool open)
{
    const qreal target = open ? 1.0 : 0.0;
    if (qFuzzyCompare(m_open + 1, target + 1) && m_anim->state() != QAbstractAnimation::Running)
        return;
    m_anim->stop();
    m_anim->setStartValue(m_open);
    m_anim->setEndValue(target);
    m_anim->start();
}

void NowIsland::enterEvent(QEnterEvent *)
{
    m_wantOpen = true;
    m_hover->start(m_open > 0 ? 0 : 350);
}

void NowIsland::leaveEvent(QEvent *)
{
    m_wantOpen = false;
    m_hover->start(250);
}

void NowIsland::mousePressEvent(QMouseEvent *event)
{
    // The pill, or the card's picture and words: to the video.
    if (event->button() != Qt::LeftButton || !body().contains(event->position()))
        return;
    m_hover->stop();
    m_anim->stop();
    m_open = 0;
    m_wantOpen = false;
    if (onOpen)
        onOpen();
}

void NowIsland::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const theme::Palette &c = theme::c();
    const QRectF b = body();
    const qreal t = m_open;
    const qreal radius = kPillHeight / 2.0 + (18 - kPillHeight / 2.0) * t;

    // A soft shadow that grows as it opens.
    for (int i = 10; i > 0; --i) {
        QPainterPath ring;
        ring.addRoundedRect(b.adjusted(-i * 0.7, -i * 0.3 + 2, i * 0.7, i + 2), radius + i * 0.7, radius + i * 0.7);
        p.fillPath(ring, QColor(0, 0, 0, int(4 + 6 * t)));
    }
    QPainterPath shape;
    shape.addRoundedRect(b, radius, radius);
    p.fillPath(shape, mixed(c.cardAlt, c.raised, 0.5));
    p.setPen(QPen(c.line, 1));
    p.drawPath(shape);

    // Three bars of the accent, as a sound wave, then the words.
    const auto bars = [&](qreal x, qreal cy, qreal alpha) {
        p.setPen(Qt::NoPen);
        QColor a = theme::accent();
        a.setAlphaF(alpha);
        p.setBrush(a);
        const qreal heights[3] = {9, 15, 11};
        for (int i = 0; i < 3; ++i)
            p.drawRoundedRect(QRectF(x + i * 5, cy - heights[i] / 2, 3, heights[i]), 1.5, 1.5);
    };
    QFont bold = font();
    bold.setWeight(QFont::DemiBold);
    if (t < 1) {
        const qreal alpha = 1 - qMin(1.0, t * 2.5);
        const qreal cy = b.top() + kPillHeight / 2.0;
        bars(b.left() + 16, cy, alpha);
        p.setFont(bold);
        QColor muted = c.muted, bright = c.textBright;
        muted.setAlphaF(alpha);
        bright.setAlphaF(alpha);
        const QRectF text(b.left() + 38, b.top(), b.width() - 52, kPillHeight);
        const QString lead = QStringLiteral("Now playing  ·  ");
        const qreal leadW = QFontMetricsF(bold).horizontalAdvance(lead);
        p.setPen(muted);
        p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter, lead);
        p.setPen(bright);
        p.drawText(text.adjusted(leadW, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(bold).elidedText(m_title, Qt::ElideRight, text.width() - leadW));
    }
    if (t > 0.4) {
        const qreal alpha = (t - 0.4) / 0.6;
        p.setOpacity(alpha);
        // The picture, 16:9, round cornered.
        const QRectF thumb(b.left() + 12, b.top() + 12, kThumbW, qMin<qreal>(kThumbH, b.height() - 24));
        QPainterPath round;
        round.addRoundedRect(thumb, 10, 10);
        const QPixmap cover = m_videoId.isEmpty() ? QPixmap() : m_covers->get(m_videoId, ThumbnailCache::Size::Card);
        if (cover.isNull()) {
            p.fillPath(round, c.raised);
        } else {
            p.save();
            p.setClipPath(round);
            const QSizeF s = QSizeF(cover.size()).scaled(thumb.size(), Qt::KeepAspectRatioByExpanding);
            p.drawPixmap(QRectF(thumb.center() - QPointF(s.width() / 2, s.height() / 2), s), cover, cover.rect());
            p.restore();
        }
        // Words beside it: what is playing, two lines; the channel.
        const QRectF words(thumb.right() + 14, thumb.top(), b.right() - thumb.right() - 26, 64);
        bars(words.left(), words.top() + 7, 1);
        p.setFont(font());
        p.setPen(c.muted);
        p.drawText(QRectF(words.left() + 20, words.top(), words.width() - 20, 14), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("Now playing"));
        QFont title = bold;
        title.setPointSizeF(title.pointSizeF() * 1.05);
        p.setFont(title);
        p.setPen(c.textBright);
        const QFontMetricsF tm(title);
        QString first = m_title, second;
        if (tm.horizontalAdvance(m_title) > words.width()) {
            int cut = m_title.size();
            while (cut > 0 && tm.horizontalAdvance(m_title.left(cut)) > words.width())
                --cut;
            const int space = m_title.left(cut).lastIndexOf(' ');
            if (space > 0)
                cut = space;
            first = m_title.left(cut).trimmed();
            second = tm.elidedText(m_title.mid(cut).trimmed(), Qt::ElideRight, words.width());
        }
        p.drawText(QRectF(words.left(), words.top() + 20, words.width(), tm.height()), Qt::AlignLeft | Qt::AlignVCenter, first);
        if (!second.isEmpty())
            p.drawText(QRectF(words.left(), words.top() + 20 + tm.height(), words.width(), tm.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, second);
        if (!m_channel.isEmpty()) {
            p.setFont(font());
            p.setPen(c.muted);
            p.drawText(QRectF(words.left(), words.top() + 22 + 2 * tm.height(), words.width(), 16),
                       Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(font()).elidedText(m_channel, Qt::ElideRight, words.width()));
        }
        p.setOpacity(1);
    }
}
