#include "nowplaying.h"

#include "mpvclient.h"
#include "theme.h"
#include "thumbnails.h"
#include "tiles.h"
#include "youtube.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {

// Segoe Fluent Icons on Windows 11, MDL2 Assets on 10: same code points.
const QChar kPrev(0xE892), kPlay(0xE768), kPause(0xE769), kNext(0xE893), kVolume(0xE767);

QPushButton *iconButton(QChar glyph, const QString &tip, bool big = false)
{
    auto *b = new QPushButton(QString(glyph));
    b->setObjectName(big ? "transportBig" : "transport");
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QString clock(double seconds)
{
    if (seconds < 0)
        return QStringLiteral("--:--");
    const int s = int(seconds);
    return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

QPixmap rounded(const QPixmap &cover, const QString &name, int size)
{
    QPixmap out(size * 2, size * 2);   // drawn at 2x, shown at 1x: crisp on HiDPI
    out.fill(Qt::transparent);
    QPainter p(&out);
    tiles::paintCover(p, QRectF(0, 0, size * 2, size * 2), cover, name, 12);
    p.end();
    out.setDevicePixelRatio(2);
    return out;
}

} // namespace

NowPlayingBar::NowPlayingBar(MpvClient *mpv, ThumbnailCache *covers, QWidget *parent)
    : QWidget(parent), m_mpv(mpv), m_covers(covers)
{
    setObjectName("nowPlaying");
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(88);

    m_cover = new QLabel;
    m_cover->setFixedSize(60, 60);
    m_title = new QLabel;
    m_title->setObjectName("npTitle");
    m_subtitle = new QLabel;
    m_subtitle->setObjectName("npSubtitle");
    auto *info = new QVBoxLayout;
    info->setSpacing(2);
    info->addStretch();
    info->addWidget(m_title);
    info->addWidget(m_subtitle);
    info->addStretch();
    auto *left = new QHBoxLayout;
    left->setSpacing(12);
    left->addWidget(m_cover);
    left->addLayout(info, 1);

    auto *prev = iconButton(kPrev, "Previous");
    m_playPause = iconButton(kPlay, "Play / pause", true);
    auto *next = iconButton(kNext, "Next");
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(14);
    buttons->addStretch();
    buttons->addWidget(prev);
    buttons->addWidget(m_playPause);
    buttons->addWidget(next);
    buttons->addStretch();

    m_elapsed = new QLabel("--:--");
    m_total = new QLabel("--:--");
    m_elapsed->setObjectName("npTime");
    m_total->setObjectName("npTime");
    m_position = new QSlider(Qt::Horizontal);
    m_position->setRange(0, 1000);
    auto *timeline = new QHBoxLayout;
    timeline->setSpacing(10);
    timeline->addWidget(m_elapsed);
    timeline->addWidget(m_position, 1);
    timeline->addWidget(m_total);
    auto *center = new QVBoxLayout;
    center->setSpacing(4);
    center->addLayout(buttons);
    center->addLayout(timeline);

    auto *volumeIcon = new QLabel(QString(kVolume));
    volumeIcon->setObjectName("npIcon");
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 130);
    m_volume->setFixedWidth(120);
    auto *right = new QHBoxLayout;
    right->addStretch();
    right->addWidget(volumeIcon);
    right->addWidget(m_volume);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(16, 10, 20, 10);
    row->addLayout(left, 3);
    row->addLayout(center, 4);
    row->addLayout(right, 3);

    connect(prev, &QPushButton::clicked, this, [this] { m_mpv->command({"playlist-prev"}); });
    connect(next, &QPushButton::clicked, this, [this] { m_mpv->command({"playlist-next"}); });
    connect(m_playPause, &QPushButton::clicked, this, [this] { m_mpv->command({"cycle", "pause"}); });
    connect(m_position, &QSlider::sliderPressed, this, [this] { m_dragging = true; });
    connect(m_position, &QSlider::sliderReleased, this, [this] {
        m_dragging = false;
        const double duration = m_mpv->property("duration").toDouble();
        if (duration > 0)
            m_mpv->command({"seek", duration * m_position->value() / 1000.0, "absolute"});
    });
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        if (m_volume->isSliderDown())
            m_mpv->setProperty("volume", v);
    });
    connect(m_mpv, &MpvClient::propertyChanged, this, [this](const QString &name) { onProperty(name); });
    connect(m_mpv, &MpvClient::connectionChanged, this, [this](bool on) {
        if (!on)
            showIdle();
    });
    showIdle();
}

void NowPlayingBar::showIdle()
{
    m_title->setText("Nothing playing");
    m_subtitle->setText("Pick a playlist and press play");
    m_elapsed->setText("--:--");
    m_total->setText("--:--");
    m_position->setValue(0);
    m_playPause->setText(QString(kPlay));
    m_cover->setPixmap(rounded({}, "G", 60));
}

void NowPlayingBar::refreshCover()
{
    if (!m_mpv->connected())
        return;
    QString id = youtube::videoId(m_mpv->property("user-data/ghost/source").toString());
    if (id.isEmpty())
        id = youtube::videoId(m_mpv->property("path").toString());
    const QString title = m_mpv->property("media-title").toString();
    m_cover->setPixmap(rounded(m_covers->get(id, ThumbnailCache::Size::Card), title, 60));
}

void NowPlayingBar::onProperty(const QString &name)
{
    if (name == "media-title" || name == "metadata/by-key/uploader" || name == "playlist-pos" || name == "playlist-count") {
        const QString title = m_mpv->property("media-title").toString();
        m_title->setText(QFontMetrics(m_title->font()).elidedText(title, Qt::ElideRight, m_title->width() > 50 ? m_title->width() : 260));
        const QString uploader = m_mpv->property("metadata/by-key/uploader").toString();
        const int pos = m_mpv->property("playlist-pos").toInt(), count = m_mpv->property("playlist-count").toInt();
        QString sub = count > 0 ? QStringLiteral("%1 of %2").arg(pos + 1).arg(count) : QString();
        if (!uploader.isEmpty())
            sub = uploader + (sub.isEmpty() ? QString() : QStringLiteral("  ·  ") + sub);
        m_subtitle->setText(sub);
    }
    if (name == "media-title" || name == "user-data/ghost/source" || name == "path")
        refreshCover();
    if (name == "pause")
        m_playPause->setText(QString(m_mpv->property("pause").toBool() ? kPlay : kPause));
    if (name == "time-pos" || name == "duration") {
        const double pos = m_mpv->property("time-pos").isValid() ? m_mpv->property("time-pos").toDouble() : -1;
        const double duration = m_mpv->property("duration").isValid() ? m_mpv->property("duration").toDouble() : -1;
        m_elapsed->setText(clock(pos));
        m_total->setText(clock(duration));
        if (!m_dragging)
            m_position->setValue(duration > 0 && pos >= 0 ? int(pos / duration * 1000) : 0);
    }
    if (name == "volume" && !m_volume->isSliderDown())
        m_volume->setValue(int(m_mpv->property("volume").toDouble()));
}
