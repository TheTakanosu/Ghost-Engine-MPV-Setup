#include "videopage.h"

#include "icons.h"
#include "theme.h"
#include "tiles.h"

#include <QCursor>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>

namespace {

constexpr int kSideWidth = 400;          // the list on the right
constexpr int kTargetRole = Qt::UserRole + 40;
constexpr int kNowRole = Qt::UserRole + 41;
constexpr int kCorner = 12;              // the player's rounded corners
const int kHeights[] = {0, 2160, 1440, 1080, 720, 480, 360};
const double kSpeeds[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};

QString clock(double seconds)
{
    if (seconds < 0)
        return QStringLiteral("--:--");
    const int s = int(seconds);
    return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// The black area mpv draws into: a native window of its own, so mpv can be
// given its handle. Its corners are cut round with a window region, which
// also clips mpv's window inside it; a Qt mask would not reach that far.
class Surface : public QWidget {
public:
    Surface()
    {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_DontCreateNativeAncestors);
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, Qt::black);
        setPalette(p);
        winId();   // the window exists from now on
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        const qreal dpr = devicePixelRatioF();
        const int w = int(width() * dpr), h = int(height() * dpr), r = int(2 * kCorner * dpr);
        SetWindowRgn(reinterpret_cast<HWND>(winId()), CreateRoundRectRgn(0, 0, w + 1, h + 1, r, r), TRUE);
    }
};

// A video in the list on the right: its thumbnail, title, and length, as in
// YouTube's "up next".
class VideoRow : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    std::function<QPixmap(const QString &)> cover;
    std::function<QString(const QString &)> length;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return {kSideWidth, int(84 * theme::zoom())};
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(2, 3, -2, -3);
        const bool now = index.data(kNowRole).toBool();
        const bool hover = option.state & QStyle::State_MouseOver;
        if (now || hover) {
            QPainterPath bg;
            bg.addRoundedRect(row, 8, 8);
            p->fillPath(bg, now ? theme::cardSelected() : theme::cardHover());
        }
        const QString target = index.data(kTargetRole).toString();
        const QRectF thumb(row.left() + 6, row.top() + 5, (row.height() - 10) * 16 / 9, row.height() - 10);
        tiles::paintCover(*p, thumb, cover ? cover(target) : QPixmap(), index.data(Qt::DisplayRole).toString(), 6);
        const QString len = length ? length(target) : QString();
        if (!len.isEmpty()) {
            QFont f = option.font;
            f.setPointSizeF(f.pointSizeF() * 0.85);
            f.setWeight(QFont::DemiBold);
            const QFontMetricsF fm(f);
            const QRectF badge(thumb.right() - fm.horizontalAdvance(len) - 12, thumb.bottom() - fm.height() - 6,
                               fm.horizontalAdvance(len) + 8, fm.height() + 2);
            QPainterPath b;
            b.addRoundedRect(badge, 4, 4);
            p->fillPath(b, QColor(0, 0, 0, 190));
            p->setFont(f);
            p->setPen(Qt::white);
            p->drawText(badge, Qt::AlignCenter, len);
        }
        const QRectF text(thumb.right() + 10, row.top() + 6, row.right() - thumb.right() - 16, row.height() - 12);
        QFont title = option.font;
        title.setWeight(QFont::DemiBold);
        p->setFont(title);
        p->setPen(now ? theme::accent() : theme::text());
        p->drawText(text, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, index.data(Qt::DisplayRole).toString());
        p->restore();
    }
};

QPushButton *action(const QString &glyph, const QString &label)
{
    auto *b = new QPushButton(glyph + QStringLiteral("  ") + label);
    b->setObjectName("videoAction");
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QPushButton *controlButton(const QString &text, const QString &tip)
{
    auto *b = new QPushButton(text);
    b->setObjectName("videoControl");
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

} // namespace

VideoPage::VideoPage(QWidget *parent) : QWidget(parent)
{
    m_surface = new Surface;

    // While mpv plays in its own window, this stands where the picture was.
    m_away = new QWidget;
    m_away->setObjectName("videoAway");
    m_away->setAttribute(Qt::WA_StyledBackground);
    auto *awayText = new QLabel("Playing in mpv's own window");
    awayText->setObjectName("sectionTitle");
    awayText->setAlignment(Qt::AlignCenter);
    auto *back = new QPushButton("Bring it back here");
    back->setObjectName("primary");
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QPushButton::clicked, this, [this] { if (onBringBack) onBringBack(); });
    auto *awayBox = new QVBoxLayout(m_away);
    awayBox->addStretch();
    awayBox->addWidget(awayText);
    awayBox->addSpacing(12);
    awayBox->addWidget(back, 0, Qt::AlignHCenter);
    awayBox->addStretch();

    // Only the player's height is set (from the width it gets); its width
    // is free, or the page could not narrow and the library panel beside
    // it could not be widened while a video played.
    m_player = new QStackedWidget;
    m_player->addWidget(m_surface);
    m_player->addWidget(m_away);
    m_player->setMinimumWidth(320);
    m_player->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_controls = buildControls();

    m_title = new QLabel;
    m_title->setObjectName("videoTitle");
    m_title->setWordWrap(true);
    m_channel = new QLabel;
    m_channel->setObjectName("statLabel");
    auto *popOut = action(QString(QChar(0xE8A7)), "Open in mpv's window");
    m_youtube = action(QString(QChar(0xE774)), "Watch on YouTube");
    connect(popOut, &QPushButton::clicked, this, [this] { if (onPopOut) onPopOut(); });
    connect(m_youtube, &QPushButton::clicked, this, [this] {
        if (!m_link.isEmpty())
            QDesktopServices::openUrl(QUrl(m_link));
    });
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(m_channel, 1);
    buttons->addWidget(popOut);
    buttons->addWidget(m_youtube);

    // The video's own description, as YouTube shows it under the player:
    // a few lines, then all of it on request.
    m_description = new QTextBrowser;
    m_description->setObjectName("videoDescription");
    m_description->setOpenExternalLinks(true);
    m_description->setFrameShape(QFrame::NoFrame);
    m_description->setFixedHeight(84);
    m_description->hide();
    m_more = new QPushButton("Show more");
    m_more->setObjectName("link");
    m_more->setCursor(Qt::PointingHandCursor);
    m_more->hide();
    connect(m_more, &QPushButton::clicked, this, [this] {
        m_expanded = !m_expanded;
        m_description->setFixedHeight(m_expanded ? 260 : 84);
        m_more->setText(m_expanded ? "Show less" : "Show more");
        fitPlayer();
    });

    m_info = new QWidget;
    auto *info = new QVBoxLayout(m_info);
    info->setContentsMargins(4, 8, 4, 0);
    info->setSpacing(6);
    info->addWidget(m_title);
    info->addLayout(buttons);
    info->addSpacing(4);
    info->addWidget(m_description);
    info->addWidget(m_more, 0, Qt::AlignLeft);

    auto *left = new QVBoxLayout;
    left->setSpacing(0);
    left->addWidget(m_player);
    left->addWidget(m_controls);
    left->addWidget(m_info);
    left->addStretch();

    m_listName = new QLabel;
    m_listName->setObjectName("sectionTitle");
    m_list = new QListWidget;
    m_list->setObjectName("songs");
    m_list->setMouseTracking(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    auto *rows = new VideoRow(m_list);
    rows->cover = [this](const QString &t) { return coverFor ? coverFor(t) : QPixmap(); };
    rows->length = [this](const QString &t) { return lengthFor ? lengthFor(t) : QString(); };
    m_list->setItemDelegate(rows);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (onPick)
            onPick(item->data(kTargetRole).toString());
    });
    m_side = new QWidget;
    m_side->setFixedWidth(kSideWidth);
    auto *side = new QVBoxLayout(m_side);
    side->setContentsMargins(0, 0, 0, 0);
    side->setSpacing(8);
    side->addWidget(m_listName);
    side->addWidget(m_list, 1);

    auto *box = new QHBoxLayout(this);
    box->setContentsMargins(24, 20, 20, 12);
    box->setSpacing(20);
    box->addLayout(left, 1);
    box->addWidget(m_side);

    // Full screen: the mouse moves over mpv's window, which the app does not
    // see, so its position is looked at a few times a second instead.
    m_idle.setInterval(250);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        static int still = 0;
        const QPoint at = QCursor::pos();
        if (at != m_lastMouse) {
            m_lastMouse = at;
            still = 0;
            showControls();
        } else if (++still == 12 && !m_controls->underMouse()) {   // 3 seconds
            m_controls->hide();
            fitPlayer();
        }
    });
}

QWidget *VideoPage::buildControls()
{
    auto *bar = new QWidget;
    bar->setObjectName("videoControls");
    bar->setAttribute(Qt::WA_StyledBackground);
    m_play = controlButton(QString(QChar(0xE769)), "Play / pause (Space)");
    auto *back10 = controlButton(QString(QChar(0xE72B)), "Back 10 seconds (Left)");
    auto *fwd10 = controlButton(QString(QChar(0xE72A)), "Forward 10 seconds (Right)");
    connect(m_play, &QPushButton::clicked, this, [this] { if (onTogglePause) onTogglePause(); });
    connect(back10, &QPushButton::clicked, this, [this] { if (onSkip) onSkip(-10); });
    connect(fwd10, &QPushButton::clicked, this, [this] { if (onSkip) onSkip(10); });

    m_seek = new QSlider(Qt::Horizontal);
    m_seek->setObjectName("seek");
    m_seek->setRange(0, 1000);
    connect(m_seek, &QSlider::sliderPressed, this, [this] { m_dragging = true; });
    connect(m_seek, &QSlider::sliderReleased, this, [this] {
        m_dragging = false;
        if (onSeek && m_duration > 0)
            onSeek(m_duration * m_seek->value() / 1000.0);
    });
    m_time = new QLabel(QStringLiteral("--:-- / --:--"));
    m_time->setObjectName("videoTime");

    // Subtitles, quality and speed open small menus, as YouTube's gear does.
    m_subs = controlButton(QString(QChar(0xE7F0)), "Subtitles");
    connect(m_subs, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        bool any = false;
        bool on = false;
        for (const QVariant &v : m_tracks) {
            const QVariantMap t = v.toMap();
            if (t.value("type").toString() != "sub")
                continue;
            any = true;
            QString name = t.value("title").toString();
            const QString lang = t.value("lang").toString();
            if (name.isEmpty())
                name = lang.isEmpty() ? QStringLiteral("Track %1").arg(t.value("id").toInt()) : lang;
            else if (!lang.isEmpty())
                name += QStringLiteral(" (%1)").arg(lang);
            QAction *a = menu.addAction(name);
            a->setCheckable(true);
            a->setChecked(t.value("selected").toBool());
            on |= t.value("selected").toBool();
            const QVariant id = t.value("id");
            connect(a, &QAction::triggered, this, [this, id] { if (onSubtitle) onSubtitle(id); });
        }
        if (!any) {
            menu.addAction("This video has no subtitles")->setEnabled(false);
        } else {
            menu.addSeparator();
            QAction *off = menu.addAction("Off");
            off->setCheckable(true);
            off->setChecked(!on);
            connect(off, &QAction::triggered, this, [this] { if (onSubtitle) onSubtitle(QStringLiteral("no")); });
        }
        menu.exec(m_subs->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    });
    m_quality = controlButton(QStringLiteral("Auto"), "Quality");
    m_quality->setObjectName("videoTextControl");
    connect(m_quality, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        for (int h : kHeights) {
            QAction *a = menu.addAction(h > 0 ? QStringLiteral("Up to %1p").arg(h) : QStringLiteral("Best available"));
            a->setCheckable(true);
            a->setChecked(h == m_cap);
            connect(a, &QAction::triggered, this, [this, h] { if (onQuality) onQuality(h); });
        }
        menu.exec(m_quality->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    });
    m_speed = controlButton(QStringLiteral("1x"), "Speed");
    m_speed->setObjectName("videoTextControl");
    connect(m_speed, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        for (double s : kSpeeds) {
            QAction *a = menu.addAction(s == 1.0 ? QStringLiteral("Normal") : QStringLiteral("%1x").arg(s));
            a->setCheckable(true);
            a->setChecked(qFuzzyCompare(s, m_speedNow));
            connect(a, &QAction::triggered, this, [this, s] { if (onSpeed) onSpeed(s); });
        }
        menu.exec(m_speed->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    });
    auto *full = controlButton(QString(QChar(0xE740)), "Full screen (F)");
    connect(full, &QPushButton::clicked, this, [this] { if (onFullScreen) onFullScreen(); });

    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(10, 6, 10, 6);
    row->setSpacing(6);
    row->addWidget(m_play);
    row->addWidget(back10);
    row->addWidget(fwd10);
    row->addSpacing(6);
    row->addWidget(m_time);
    row->addSpacing(6);
    row->addWidget(m_seek, 1);
    row->addSpacing(6);
    row->addWidget(m_subs);
    row->addWidget(m_quality);
    row->addWidget(m_speed);
    row->addWidget(full);
    return bar;
}

void VideoPage::setList(const QString &name, const QVector<Entry> &entries)
{
    m_listName->setText(name);
    m_list->clear();
    for (const Entry &e : entries) {
        auto *item = new QListWidgetItem(e.title);
        item->setData(kTargetRole, e.target);
        m_list->addItem(item);
    }
}

void VideoPage::setPlaying(const QString &target, const QString &title, const QString &channel, const QString &link)
{
    if (link != m_link)
        setDescription(QString());
    m_title->setText(title);
    m_channel->setText(channel);
    m_link = link;
    m_youtube->setVisible(!link.isEmpty());
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        const bool now = item->data(kTargetRole).toString() == target;
        item->setData(kNowRole, now);
        if (now)
            m_list->scrollToItem(item, QAbstractItemView::EnsureVisible);
    }
}

void VideoPage::setDescription(const QString &text)
{
    // Plain text with its links made clickable, as YouTube does.
    QString html = text.toHtmlEscaped();
    static const QRegularExpression url(QStringLiteral(R"((https?://[^\s<]+))"));
    html.replace(url, QStringLiteral("<a href=\"\\1\" style=\"color: rgb(176,127,224);\">\\1</a>"));
    html.replace('\n', QStringLiteral("<br>"));
    m_description->setHtml(html);
    m_description->setVisible(!text.trimmed().isEmpty() && !m_full);
    m_more->setVisible(!text.trimmed().isEmpty() && !m_full);
    m_expanded = false;
    m_description->setFixedHeight(84);
    m_more->setText("Show more");
    fitPlayer();
}

void VideoPage::setTimes(double position, double duration)
{
    m_duration = duration;
    m_time->setText(clock(position) + QStringLiteral(" / ") + clock(duration));
    if (!m_dragging)
        m_seek->setValue(duration > 0 && position >= 0 ? int(position / duration * 1000) : 0);
}

void VideoPage::setPaused(bool paused)
{
    m_play->setText(QString(QChar(paused ? 0xE768 : 0xE769)));
}

void VideoPage::setTracks(const QVariantList &tracks)
{
    m_tracks = tracks;
    bool on = false;
    for (const QVariant &v : tracks)
        if (v.toMap().value("type").toString() == "sub" && v.toMap().value("selected").toBool())
            on = true;
    m_subs->setStyleSheet(on ? QStringLiteral("color: rgb(176,127,224);") : QString());
}

void VideoPage::setQualityCap(int height)
{
    m_cap = height;
    m_quality->setText(height > 0 ? QStringLiteral("%1p").arg(height) : QStringLiteral("Best"));
}

void VideoPage::setSpeed(double speed)
{
    m_speedNow = speed;
    m_speed->setText(qFuzzyCompare(speed, 1.0) ? QStringLiteral("1x") : QStringLiteral("%1x").arg(speed));
}

void VideoPage::setPoppedOut(bool out)
{
    m_player->setCurrentWidget(out ? m_away : m_surface);
    m_controls->setVisible(!out);
}

void VideoPage::showControls()
{
    if (!m_controls->isVisible()) {
        m_controls->show();
        fitPlayer();
    }
}

void VideoPage::setFullScreen(bool full)
{
    m_full = full;
    m_info->setVisible(!full);
    m_side->setVisible(!full);
    layout()->setContentsMargins(full ? QMargins() : QMargins(24, 20, 20, 12));
    m_controls->show();
    if (full) {
        m_lastMouse = QCursor::pos();
        m_idle.start();
    } else {
        m_idle.stop();
    }
    fitPlayer();
}

void VideoPage::refreshCovers() const
{
    m_list->viewport()->update();
}

void VideoPage::resizeEvent(QResizeEvent *)
{
    fitPlayer();
}

void VideoPage::fitPlayer()
{
    // 16:9 for the width there is, short enough to leave the controls and
    // the title in view; in full screen, everything the controls leave.
    const int controls = m_controls->isVisible() ? m_controls->sizeHint().height() : 0;
    if (m_full) {
        m_player->setFixedHeight(qMax(180, height() - controls));
        return;
    }
    // Everything under the picture keeps its own height; the picture takes
    // what is left (or 16:9, if less). A layout given more than it has
    // would lay the controls over the video, which hides them.
    const int width = this->width() - kSideWidth - 64;
    const int under = controls + m_info->sizeHint().height() + 20 + 12;
    m_player->setFixedHeight(qMax(180, qMin(width * 9 / 16, this->height() - under)));
}
