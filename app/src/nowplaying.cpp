#include "nowplaying.h"

#include "icons.h"
#include "mpvclient.h"
#include "theme.h"
#include "thumbnails.h"
#include "tiles.h"
#include "youtube.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

constexpr int kVolumeStep = 5;   // per wheel notch on the volume slider

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
    const QRectF box(0, 0, size * 2, size * 2);
    tiles::paintCover(p, box, cover, name, tiles::playlistRadius(box, 12));
    p.end();
    out.setDevicePixelRatio(2);
    return out;
}

// Volume icon drawn like the rest: a speaker and two arcs.
class SpeakerIcon : public QAbstractButton {
public:
    SpeakerIcon()
    {
        setFixedSize(24, 22);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
    }
    // 0 (or muted) draws a cross; otherwise as many waves as the level earns.
    void setLevel(int volume, bool muted)
    {
        const int waves = muted || volume <= 0 ? 0 : volume < 50 ? 1 : volume < 75 ? 2 : 3;
        if (waves != m_waves) {
            m_waves = waves;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor ink = underMouse() ? Qt::white : theme::muted();
        p.setPen(QPen(ink, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(ink);
        p.drawPolygon(QPolygonF({{3, 9}, {7, 9}, {11, 5}, {11, 17}, {7, 13}, {3, 13}}));
        p.setBrush(Qt::NoBrush);
        if (m_waves == 0) {
            p.drawLine(QPointF(15, 8), QPointF(21, 14));
            p.drawLine(QPointF(21, 8), QPointF(15, 14));
            return;
        }
        const QRectF arcs[] = {{10, 8, 5, 6}, {10, 5, 9, 12}, {10, 2, 13, 18}};
        for (int i = 0; i < m_waves; ++i)
            p.drawArc(arcs[i], -55 * 16, 110 * 16);
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    int m_waves = 2;
};

} // namespace

// mpv reports a loop as "inf", a count, or false (JSON) when off.
static bool looping(const QVariant &v)
{
    return v.isValid() && v.toString() != QLatin1String("no") && v.toString() != QLatin1String("false");
}

NowPlayingBar::NowPlayingBar(MpvClient *mpv, ThumbnailCache *covers, QWidget *parent)
    : QWidget(parent), m_mpv(mpv), m_covers(covers)
{
    setObjectName("nowPlaying");
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(90);

    m_cover = new QLabel;
    m_cover->setFixedSize(58, 58);
    m_cover->setCursor(Qt::PointingHandCursor);
    m_cover->setToolTip("Show the playlist");
    m_cover->installEventFilter(this);
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
    auto *leftBox = new QWidget;
    auto *left = new QHBoxLayout(leftBox);
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(12);
    left->addWidget(m_cover);
    left->addLayout(info, 1);

    auto *prev = new icons::IconButton(icons::Icon::Previous, 32, "Previous");
    m_playPause = new icons::IconButton(icons::Icon::Play, 40, "Play / pause");
    m_playPause->setRound(theme::text(), Qt::white, theme::window());
    auto *next = new icons::IconButton(icons::Icon::Next, 32, "Next");
    for (auto *b : {prev, next})
        b->setColors(QColor(200, 198, 210), Qt::white);
    // Shuffle and repeat act on what is playing, as in Spotify; the shuffle
    // on a playlist page starts that list shuffled instead.
    m_shuffle = new icons::IconButton(icons::Icon::Shuffle, 32, "Shuffle what is playing");
    m_repeat = new icons::IconButton(icons::Icon::Repeat, 32, "Repeat");
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(20);
    buttons->addStretch();
    buttons->addWidget(m_shuffle);
    buttons->addWidget(prev);
    buttons->addWidget(m_playPause);
    buttons->addWidget(next);
    buttons->addWidget(m_repeat);
    buttons->addStretch();

    m_elapsed = new QLabel("--:--");
    m_total = new QLabel("--:--");
    m_elapsed->setObjectName("npTime");
    m_total->setObjectName("npTime");
    m_elapsed->setFixedWidth(44);
    m_total->setFixedWidth(44);
    m_elapsed->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_position = new QSlider(Qt::Horizontal);
    m_position->setObjectName("seek");
    m_position->setRange(0, 1000);
    m_position->installEventFilter(this);
    auto *timeline = new QHBoxLayout;
    timeline->setSpacing(10);
    timeline->addWidget(m_elapsed);
    timeline->addWidget(m_position, 1);
    timeline->addWidget(m_total);
    auto *centerBox = new QWidget;
    auto *center = new QVBoxLayout(centerBox);
    center->setContentsMargins(0, 0, 0, 0);
    center->setSpacing(6);
    center->addLayout(buttons);
    center->addLayout(timeline);

    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 130);
    m_volume->setFixedWidth(120);
    m_volume->installEventFilter(this);
    auto *rightBox = new QWidget;
    auto *right = new QHBoxLayout(rightBox);
    right->setContentsMargins(0, 0, 0, 0);
    right->addStretch();
    m_lyrics = new QPushButton(QString(QChar(0xE720)));   // a microphone
    m_lyrics->setObjectName("glyph");
    m_lyrics->setToolTip("Lyrics");
    m_lyrics->setCursor(Qt::PointingHandCursor);
    connect(m_lyrics, &QPushButton::clicked, this, &NowPlayingBar::toggleLyrics);
    right->addWidget(m_lyrics);
    right->addSpacing(6);
    m_speaker = new SpeakerIcon;
    connect(m_speaker, &QAbstractButton::clicked, this, [this] { m_mpv->command({"cycle", "mute"}); });
    right->addWidget(m_speaker);
    right->addWidget(m_volume);
    m_volumeNumber = new QLabel;
    m_volumeNumber->setObjectName("npVolume");
    m_volumeNumber->setFixedWidth(46);
    m_volumeNumber->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // Bold digits sit low in their line; two pixels up centres them on the bar.
    m_volumeNumber->setContentsMargins(8, 0, 0, 2);
    m_volumeNumber->setVisible(false);
    right->addWidget(m_volumeNumber, 0, Qt::AlignVCenter);
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        m_volumeNumber->setText(QStringLiteral("%1%").arg(v));
        static_cast<SpeakerIcon *>(m_speaker)->setLevel(v, m_mpv->property("mute").toBool());
    });
    static_cast<SpeakerIcon *>(m_speaker)->setLevel(m_volume->value(), false);   // before mpv says anything

    // Equal side columns keep the controls on the window's centre line,
    // whatever the title on the left is.
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(16, 8, 20, 10);
    grid->setHorizontalSpacing(16);
    grid->addWidget(leftBox, 0, 0);
    grid->addWidget(centerBox, 0, 1);
    grid->addWidget(rightBox, 0, 2);
    grid->setColumnStretch(0, 3);
    grid->setColumnStretch(1, 4);
    grid->setColumnStretch(2, 3);
    leftBox->setMinimumWidth(0);
    leftBox->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    rightBox->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    connect(prev, &QAbstractButton::clicked, this, [this] { m_mpv->command({"playlist-prev"}); });
    connect(next, &QAbstractButton::clicked, this, [this] { m_mpv->command({"playlist-next"}); });
    connect(m_playPause, &QAbstractButton::clicked, this, [this] { m_mpv->command({"cycle", "pause"}); });
    connect(m_shuffle, &QAbstractButton::clicked, this, [this] {
        if (!m_mpv->connected())
            return;
        // Unshuffling puts the list back in its own order, the song playing
        // keeps playing.
        m_mpv->command({m_shuffled ? "playlist-unshuffle" : "playlist-shuffle"});
        m_shuffled = !m_shuffled;
        showShuffle();
    });
    connect(m_repeat, &QAbstractButton::clicked, this, [this] {
        // Off, the whole list, this song, off again.
        const bool all = looping(m_mpv->property("loop-playlist"));
        const bool one = looping(m_mpv->property("loop-file"));
        if (one) {
            m_mpv->setProperty("loop-file", "no");
            m_mpv->setProperty("loop-playlist", "no");
        } else if (all) {
            m_mpv->setProperty("loop-file", "inf");
        } else {
            m_mpv->setProperty("loop-playlist", "inf");
        }
    });
    connect(m_position, &QSlider::sliderPressed, this, [this] { m_dragging = true; });
    connect(m_position, &QSlider::sliderReleased, this, [this] {
        m_dragging = false;
        const double duration = m_mpv->property("duration").toDouble();
        if (duration > 0)
            m_mpv->command({"seek", duration * m_position->value() / 1000.0, "absolute"});
    });
    connect(m_volume, &QSlider::sliderMoved, this, [this](int v) { m_mpv->setProperty("volume", v); });
    connect(m_mpv, &MpvClient::propertyChanged, this, [this](const QString &name) { onProperty(name); });
    connect(m_mpv, &MpvClient::connectionChanged, this, [this](bool on) {
        if (!on)
            showIdle();
    });
    showIdle();
}

bool NowPlayingBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_cover && event->type() == QEvent::MouseButtonRelease) {
        emit openPlaying();
        return true;
    }
    if (event->type() != QEvent::Wheel)
        return QWidget::eventFilter(watched, event);
    // The wheel moved the seek bar's handle without moving the song. It now
    // does nothing there, and turns the volume up and down on its slider.
    if (watched == m_volume) {
        const int notches = static_cast<QWheelEvent *>(event)->angleDelta().y() / 120;
        const int v = qBound(0, m_volume->value() + notches * kVolumeStep, m_volume->maximum());
        m_volume->setValue(v);
        m_mpv->setProperty("volume", v);
    }
    return true;
}

void NowPlayingBar::showIdle()
{
    m_title->setText("Nothing playing");
    m_subtitle->setText("Pick a playlist and press play");
    m_elapsed->setText("--:--");
    m_total->setText("--:--");
    m_position->setValue(0);
    m_playPause->setIcon(icons::Icon::Play);
    m_cover->setPixmap(rounded({}, "G", 58));
    m_shuffled = false;
    showShuffle();
    showRepeat();
}

void NowPlayingBar::setLyricsOpen(bool open)
{
    m_lyrics->setStyleSheet(open ? QStringLiteral("color: rgb(176,127,224);") : QString());
}

void NowPlayingBar::setVolumeNumber(bool shown)
{
    m_volumeNumber->setText(QStringLiteral("%1%").arg(m_volume->value()));
    m_volumeNumber->setVisible(shown);
}

void NowPlayingBar::refreshPosition()
{
    if (m_mpv->connected())
        onProperty("playlist-pos");
}

void NowPlayingBar::expectList(bool shuffled)
{
    m_expect = shuffled ? 1 : 0;
    m_shuffled = shuffled;
    showShuffle();
}

void NowPlayingBar::showShuffle()
{
    const QColor on = theme::accent();
    m_shuffle->setColors(m_shuffled ? on : QColor(170, 168, 182), m_shuffled ? on.lighter(120) : Qt::white);
    m_shuffle->setDot(m_shuffled);
    m_shuffle->setToolTip(m_shuffled ? "Shuffle is on: back to the list's order" : "Shuffle what is playing");
}

void NowPlayingBar::showRepeat()
{
    const bool one = looping(m_mpv->property("loop-file")), all = looping(m_mpv->property("loop-playlist"));
    const QColor on = theme::accent();
    m_repeat->setIcon(one ? icons::Icon::RepeatOne : icons::Icon::Repeat);
    m_repeat->setColors(one || all ? on : QColor(170, 168, 182), one || all ? on.lighter(120) : Qt::white);
    m_repeat->setDot(one || all);
    m_repeat->setToolTip(one ? "Repeating this song" : all ? "Repeating the list" : "Repeat is off");
}

void NowPlayingBar::refreshCover()
{
    if (!m_mpv->connected()) {
        m_cover->setPixmap(rounded({}, "G", 58));   // in the shape the settings ask for
        return;
    }
    QString id = youtube::videoId(m_mpv->property("user-data/ghost/source").toString());
    if (id.isEmpty())
        id = youtube::videoId(m_mpv->property("path").toString());
    const QString title = m_mpv->property("media-title").toString();
    m_cover->setPixmap(rounded(m_covers->get(id, ThumbnailCache::Size::Card), title, 58));
}

void NowPlayingBar::onProperty(const QString &name)
{
    if (name == "media-title" || name == "metadata/by-key/uploader" || name == "playlist-pos" ||
        name == "playlist-count" || name == "path") {
        const QString title = m_mpv->property("media-title").toString();
        m_title->setText(QFontMetrics(m_title->font()).elidedText(title, Qt::ElideRight, qMax(120, m_title->width())));
        m_title->setToolTip(title);
        const QString uploader = m_mpv->property("metadata/by-key/uploader").toString();
        const int count = m_mpv->property("playlist-count").toInt();
        const int number = numberFor ? numberFor() : 0;
        const int pos = number > 0 ? number : m_mpv->property("playlist-pos").toInt() + 1;
        QString sub = count > 0 ? QStringLiteral("%1 of %2").arg(pos).arg(count) : QString();
        if (!uploader.isEmpty())
            sub = uploader + (sub.isEmpty() ? QString() : QStringLiteral("  ·  ") + sub);
        m_subtitle->setText(sub);
    }
    if (name == "media-title" || name == "user-data/ghost/source" || name == "path")
        refreshCover();
    if (name == "pause")
        m_playPause->setIcon(m_mpv->property("pause").toBool() ? icons::Icon::Play : icons::Icon::Pause);
    if (name == "time-pos" || name == "duration") {
        const double pos = m_mpv->property("time-pos").isValid() ? m_mpv->property("time-pos").toDouble() : -1;
        const double duration = m_mpv->property("duration").isValid() ? m_mpv->property("duration").toDouble() : -1;
        m_elapsed->setText(clock(pos));
        m_total->setText(clock(duration));
        if (!m_dragging)
            m_position->setValue(duration > 0 && pos >= 0 ? int(pos / duration * 1000) : 0);
    }
    if (name == "loop-file" || name == "loop-playlist")
        showRepeat();
    if (name == "playlist-path") {
        // A new list is in its own order, unless this app asked otherwise.
        m_shuffled = m_expect == 1;
        m_expect = -1;
        showShuffle();
    }
    if (name == "mute" || name == "volume")
        static_cast<SpeakerIcon *>(m_speaker)->setLevel(int(m_mpv->property("volume").toDouble()),
                                                        m_mpv->property("mute").toBool());
    if (name == "volume" && !m_volume->isSliderDown())
        m_volume->setValue(int(m_mpv->property("volume").toDouble()));
}
