#include "mainwindow.h"

#include "equalizer.h"
#include "island.h"
#include "listheader.h"
#include "searchpopup.h"

#include "covercrop.h"
#include "settings.h"
#include "spotifyimport.h"
#include "songinfo.h"
#include "traymenu.h"
#include "updates.h"
#include "videopage.h"
#include "icons.h"
#include "nowplaying.h"
#include "theme.h"
#include "tiles.h"

#include <utility>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QCollator>
#include <QContextMenuEvent>
#include <QColorDialog>
#include <QCoreApplication>
#include <QCursor>
#include <QDropEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QShortcut>
#include <QHBoxLayout>
#include <QStandardPaths>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QWindow>
#include <QInputDialog>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <cmath>
#include <memory>
#include <QPainterPath>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QClipboard>
#include <QMimeData>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QScreen>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QSettings>
#include <QSplitter>
#include <QSystemTrayIcon>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <numeric>
#include <random>

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <dwmapi.h>

namespace {

constexpr int kTile = 168;          // cover size on the Home shelves
constexpr int kShelfCount = 8;      // tiles on each Home shelf
constexpr int kMinutesPerSong = 3;  // for the "hours of music" estimate
// The README's link for adding the bot to a server.
constexpr const char *kBotInvite =
    "https://discord.com/oauth2/authorize?client_id=1530546215303909518&permissions=117760&scope=bot";
constexpr int kTopBar = 50;         // height of the draggable strip: 8 px under the search box
constexpr int kCompactWidth = 84;   // the library panel, covers only
// These at the zoom: at 100% they held the zoomed title and cover, and at
// 160% the header's text ran up over its top and the covers out of the panel.
int headerTall() { return listheader::tall(); }
int headerShort() { return listheader::folded(); }
int compactWidth() { return qRound(kCompactWidth * theme::zoom()); }
constexpr int kCompactBelow = 150;  // narrower than this, the panel goes compact

// Segoe Fluent Icons (Windows 11) / MDL2 Assets (10): same code points.
const QChar kIconHome(0xE80F), kIconMin(0xE921), kIconMax(0xE922), kIconRestore(0xE923),
    kIconClose(0xE8BB), kIconCollapse(0xE76B), kIconExpand(0xE76C), kIconAdd(0xE710),
    kIconDense(0xE8FD), kIconCovers(0xE8B9);

QString subtitleFor(const Playlist &p)
{
    return QStringLiteral("%1 · %2 %3").arg(p.audio ? "Songs" : "Videos").arg(p.count)
                                       .arg(p.audio ? (p.count == 1 ? "song" : "songs")
                                                    : (p.count == 1 ? "video" : "videos"));
}

QPushButton *glyphButton(QChar glyph, const QString &tip, const char *name = "glyph")
{
    auto *b = new QPushButton(QString(glyph));
    b->setObjectName(name);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QWidget *statCard(QLabel *&value, const QString &label)
{
    auto *card = new QWidget;
    card->setObjectName("statCard");
    card->setAttribute(Qt::WA_StyledBackground);
    value = new QLabel("0");
    value->setObjectName("statValue");
    auto *caption = new QLabel(label);
    caption->setObjectName("statLabel");
    // Onto a second line in a narrow page, rather than holding the page
    // wider than its view and the last card cut off at the right.
    caption->setWordWrap(true);
    auto *box = new QVBoxLayout(card);
    box->setContentsMargins(18, 14, 18, 14);
    box->setSpacing(2);
    box->addWidget(value);
    box->addWidget(caption);
    return card;
}

QLabel *sectionTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("sectionTitle");
    return label;
}

QString greetingFor(const QTime &now)
{
    if (now.hour() < 5)  return "Good night";
    if (now.hour() < 12) return "Good morning";
    if (now.hour() < 18) return "Good afternoon";
    return "Good evening";
}

// "#EXTINF:-1, Artist - Track": the bot's titles are artist first.
std::pair<QString, QString> splitTitle(const QString &title)
{
    const int dash = title.indexOf(QStringLiteral(" - "));
    if (dash < 0)
        return {title, QString()};
    return {title.mid(dash + 3).trimmed(), title.left(dash).trimmed()};
}

} // namespace


// A live stream: "LIVE" this close to the end of its timeline, "Go live"
// to this far from it (2026-11-12).
constexpr double kLiveBehind = 25;
constexpr double kLiveGoTo = 12;

// A followed channel's name, kept apart from the row's text (empty when folded).
constexpr int kChannelNameRole = Qt::UserRole + 41;

// mpv reports a loop as "inf", a count, or false (JSON) when off.
static bool mpvLooping(const QVariant &v)
{
    return v.isValid() && v.toString() != QLatin1String("no") && v.toString() != QLatin1String("false");
}

// Recommended under a list (Kaptan, 2026-11-12): a sheet rising from the
// page's bottom, on the page itself with no gap, a step lighter than it, its
// top corners round and a small grip on its top edge. Its bottom corners are
// the page's own.
class RecSheet : public QWidget {
public:
    explicit RecSheet(QWidget *parent = nullptr) : QWidget(parent) { setMouseTracking(true); }

    // Set by the window: the shut height (the grip and the header row), and
    // what a drag of the top edge does.
    std::function<int()> shutHeight;
    std::function<void(int height)> onDrag;      // follows the pointer
    std::function<void(int height)> onDragEnd;   // let go after a drag
    std::function<void()> onClick;               // a plain click: open or shut

    static int grip() { return 16; }   // the band at the top that holds the grip

protected:
    void paintEvent(QPaintEvent *) override
    {
        // Its own corners, clipped the way the page card's are: outside the
        // bottom ones is the window's ground, outside the top ones the page;
        // the sheet's rounded fill goes over both, so no rim of the page's or
        // the card's colour shows at a corner (Batin, 2026-11-12).
        QPainter p(this);
        constexpr qreal r = 12;
        const QRectF box(rect());
        p.fillRect(rect(), theme::ground());
        p.fillRect(QRectF(0, 0, width(), height() - r), theme::panel());
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath sheet;
        sheet.addRoundedRect(box, r, r);
        const QColor fill = theme::panelRaised();
        p.fillPath(sheet, fill);
        // The grip in one solid colour between the sheet's and the muted
        // text's, so its rounded ends leave no trace of another colour.
        const QColor m = theme::muted();
        const QColor grip(int(fill.red() + (m.red() - fill.red()) * 0.55), int(fill.green() + (m.green() - fill.green()) * 0.55),
                          int(fill.blue() + (m.blue() - fill.blue()) * 0.55));
        const qreal w = qRound(36 * theme::zoom()), h = 4;
        QPainterPath bar;
        bar.addRoundedRect(QRectF(qRound((width() - w) / 2), 6, w, h), h / 2, h / 2);
        p.fillPath(bar, grip);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton || event->position().y() > (shutHeight ? shutHeight() : grip())) {
            QWidget::mousePressEvent(event);
            return;
        }
        m_pressY = event->globalPosition().y();
        m_pressHeight = height();
        m_pressed = true;
        m_dragging = false;
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_pressed) {
            // Over the grip, the pointer says the sheet can be pulled.
            setCursor(event->position().y() < grip() ? Qt::SizeVerCursor : Qt::ArrowCursor);
            return;
        }
        const int moved = qRound(m_pressY - event->globalPosition().y());
        if (!m_dragging && qAbs(moved) < 4)
            return;
        m_dragging = true;
        setCursor(Qt::SizeVerCursor);
        if (onDrag)
            onDrag(m_pressHeight + moved);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!m_pressed || event->button() != Qt::LeftButton)
            return;
        m_pressed = false;
        if (m_dragging) {
            m_dragging = false;
            if (onDragEnd)
                onDragEnd(m_pressHeight + qRound(m_pressY - event->globalPosition().y()));
        } else if (onClick) {
            onClick();
        }
    }
    void leaveEvent(QEvent *) override
    {
        if (!m_pressed)
            unsetCursor();
    }

private:
    bool m_pressed = false;
    bool m_dragging = false;
    qreal m_pressY = 0;
    int m_pressHeight = 0;
};

// Wheel scrolling that glides instead of jumping three rows a notch: each
// notch moves the target 110 px and the view eases to it, as Spotify's does.
class SmoothScroll : public QObject {
public:
    explicit SmoothScroll(QAbstractScrollArea *area) : QObject(area), m_bar(area->verticalScrollBar())
    {
        area->viewport()->installEventFilter(this);
        // Stepped once a frame of the screen, not on Qt's animation clock,
        // which ticks at 60 a second whatever the screen shows: on a 144 or
        // 280 Hz screen the glide moved in visible steps.
        m_tick.setTimerType(Qt::PreciseTimer);
        connect(&m_tick, &QTimer::timeout, this, &SmoothScroll::step);
        // Dragged by its handle: the handle follows the mouse at once, the
        // page at most once a frame of the screen. Moved with every report of a gaming
        // mouse (1000 a second, some 8000), each one laid out and painted
        // the page again and it fell behind the mouse.
        m_bar->setTracking(false);
        m_frame.setSingleShot(true);
        m_frame.setInterval(16);
        connect(m_bar, &QScrollBar::sliderMoved, this, [this](int to) {
            m_dragTo = to;
            if (!m_frame.isActive())
                m_frame.start();
        });
        connect(m_bar, &QScrollBar::sliderPressed, this, [this] {
            m_tick.stop();   // the handle wins over a glide still running
            m_frame.setInterval(frameMs());
        });
        connect(&m_frame, &QTimer::timeout, this, [this] { m_bar->setValue(m_dragTo); });
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() != QEvent::Wheel)
            return false;
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers() != Qt::NoModifier || wheel->angleDelta().y() == 0)
            return false;
        const int from = m_tick.isActive() ? m_target : m_bar->value();
        m_target = qBound(m_bar->minimum(), from - wheel->angleDelta().y() * 110 / 120, m_bar->maximum());
        m_start = m_bar->value();
        m_clock.start();
        m_tick.setInterval(frameMs());
        m_tick.start();
        step();
        return true;
    }

private:
    static constexpr qreal kGlideMs = 220;

    // A frame of the screen the page is on: 60 Hz, 144, 280...
    int frameMs() const
    {
        const QScreen *screen = m_bar->screen();
        const qreal hz = screen && screen->refreshRate() > 1 ? screen->refreshRate() : 60;
        return qMax(2, int(1000 / hz));
    }
    void step()
    {
        const qreal t = qMin(1.0, m_clock.elapsed() / kGlideMs);
        m_bar->setValue(qRound(m_start + (m_target - m_start) * m_ease.valueForProgress(t)));
        if (t >= 1.0)
            m_tick.stop();
    }

    QScrollBar *m_bar;
    QTimer m_tick;
    QElapsedTimer m_clock;
    QEasingCurve m_ease{QEasingCurve::OutCubic};
    int m_start = 0;
    int m_target = 0;
    QTimer m_frame;
    int m_dragTo = 0;
};

// The library panel, where lists are dragged into folders, out of them and
// into a new place. Qt's own move would only shuffle rows on screen; the drop
// is handed over instead, and the panel is filled again from the files.
class LibraryList : public QListWidget {
public:
    std::function<void(const QStringList &moving, const QString &target, int position)> onDrop;

protected:
    void dropEvent(QDropEvent *event) override
    {
        if (event->source() != this) {
            event->ignore();
            return;
        }
        QStringList moving;
        for (QListWidgetItem *item : selectedItems())
            moving << item->data(tiles::kKeyRole).toString();
        const QListWidgetItem *target = itemAt(event->position().toPoint());
        const QString targetKey = target ? target->data(tiles::kKeyRole).toString() : QString();
        const int position = int(dropIndicatorPosition());
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        // After the drag has finished with the rows it is holding.
        QTimer::singleShot(0, this, [this, moving, targetKey, position] {
            if (onDrop)
                onDrop(moving, targetKey, position);
        });
    }
};

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("Ghost Engine"));
    setMinimumSize(760, 520);
    resize(1240, 800);

    // No Windows frame: our own strip at the top moves the window, the edges
    // resize it, and Windows still snaps and animates it (see nativeEvent).
    setWindowFlags(Qt::FramelessWindowHint);
    HWND hwnd = reinterpret_cast<HWND>(winId());
    const int roundCorners = 2;    // DWMWA_WINDOW_CORNER_PREFERENCE: round
    DwmSetWindowAttribute(hwnd, 33, &roundCorners, sizeof(roundCorners));
    const int dark = 1;            // DWMWA_USE_IMMERSIVE_DARK_MODE
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    SetWindowLongW(hwnd, GWL_STYLE,
                   GetWindowLongW(hwnd, GWL_STYLE) | WS_THICKFRAME | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);

    m_pages = new QStackedWidget;
    m_pages->addWidget(buildHome());
    m_listPage = buildPlaylistPage();
    m_pages->addWidget(m_listPage);
    m_settings = new SettingsPage;
    m_pages->addWidget(m_settings);
    m_lyricsView = new LyricsView;
    m_pages->addWidget(m_lyricsView);
    m_videoPage = new VideoPage;
    m_pages->addWidget(m_videoPage);
    m_profilePage = new ProfilePage;
    m_profilePage->onMoreHistory = [this] {
        m_historyShown += 50;
        refreshProfile();
    };
    m_topLists = makeShelf();
    m_profilePage->setListsShelf(m_topLists);
    m_profilePage->onOpenPlaying = [this] { if (m_mpv.connected()) openPlayingList(); };
    auto *profileScroll = new QScrollArea;
    profileScroll->setWidget(m_profilePage);
    profileScroll->setWidgetResizable(true);
    profileScroll->setFrameShape(QFrame::NoFrame);
    m_profileScroll = profileScroll;
    m_pages->addWidget(m_profileScroll);
    m_searchPage = new SearchPage(m_covers);
    m_searchPage->setRegion(ytmusic::searchRegion());
    m_ytClient.setRegion(ytmusic::searchRegion());
    m_searchPage->libraryMatches = [this](const QString &query) {
        QVector<SearchPage::LibraryMatch> out;
        for (const Playlist &p : m_library.playlists())
            if (p.name.contains(query, Qt::CaseInsensitive))
                out << SearchPage::LibraryMatch{p.name, p.path};
        return out;
    };
    m_searchPage->onPlay = [this](const QVector<ytmusic::Item> &items, const QString &name, int start, bool shuffle, bool audio) {
        playFound(items, name, start, shuffle, audio);
    };
    m_searchPage->onSave = [this](const QString &name, const QVector<ytmusic::Item> &items) { saveFound(name, items); };
    m_searchPage->onMenu = [this](const QVector<ytmusic::Item> &items, const QPoint &at) { foundMenu(items, at); };
    m_searchPage->onToast = [this](const QString &text) { toast(text); };
    m_searchPage->onLike = [this](const ytmusic::Item &it) { toggleLiked(it.target(), it.lineTitle()); };
    m_searchPage->onLikeMenu = [this](const ytmusic::Item &it, const QPoint &global) {
        likeMenu(it.target(), it.lineTitle(), global);
    };
    // A channel's page is none of the menu's items.
    m_searchPage->isFollowed = [this](const QString &id) { return follows(id); };
    m_searchPage->onFollow = [this](const QString &id, const QString &name, bool follow) { setFollowing(id, name, follow); };
    m_searchPage->onChannelShown = [this](const QString &channelId, const QString &) {
        // A followed channel opened from anywhere (a card, a search) is lit
        // on the left too, as when it is clicked there.
        markYouTubeMenu(-1, -1, channelId);
    };
    // The YouTube home is made from the videos watched here (newest first)
    // and the channels watched most.
    // Newest first, but the newest of each channel before a second of any:
    // the home asks beside a spread of what was watched, not ten videos of
    // the one channel watched last (Batın, 2026-11-12).
    m_searchPage->watchedVideos = [this] {
        QStringList first, rest;
        QSet<QString> channels;
        for (const ListeningStats::Entry &e : m_stats.history()) {
            if (!e.video || e.id.isEmpty() || first.contains(e.id) || rest.contains(e.id))
                continue;
            const QString channel = e.artist.trimmed().toLower();
            if (channel.isEmpty() || !channels.contains(channel)) {
                channels.insert(channel);
                first << e.id;
            } else {
                rest << e.id;
            }
        }
        return first + rest;
    };
    m_searchPage->watchedChannels = [this] {
        QHash<QString, int> plays;
        for (const ListeningStats::Entry &e : m_stats.history())
            if (e.video && !e.artist.trimmed().isEmpty())
                ++plays[e.artist.trimmed()];
        QStringList names = plays.keys();
        std::sort(names.begin(), names.end(), [&plays](const QString &a, const QString &b) {
            return plays.value(a) != plays.value(b) ? plays.value(a) > plays.value(b) : a < b;
        });
        return names;
    };
    m_searchPage->onOpenLibrary = [this](const QString &path) {
        const auto &all = m_library.playlists();
        for (int i = 0; i < all.size(); ++i)
            if (all[i].path.compare(path, Qt::CaseInsensitive) == 0)
                openPlaylist(i);
    };
    m_searchPage->onQueryShown = [this](const QString &query) {
        // The box says what the results are for, without searching again.
        const QSignalBlocker quiet(m_search);
        m_searchTimer.stop();
        m_search->setText(query);
        if (!query.isEmpty())
            markYouTubeMenu(kYtSearch);   // results again: Search is lit
    };
    m_pages->addWidget(m_searchPage);
    {
        auto *page = new ShortcutsPage;
        page->onToast = [this](const QString &text) { toast(text); };
        auto *scroll = new QScrollArea;
        scroll->setWidget(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        m_shortcutsScroll = scroll;
        m_pages->addWidget(scroll);
    }
    m_videoPage->coverFor = [this](const QString &target) {
        return m_covers.get(idForTarget(target), ThumbnailCache::Size::Card);
    };
    m_videoPage->channelFor = [this](const QString &target) { return videoByline(idForTarget(target)); };
    m_videoPage->lengthFor = [this](const QString &target) {
        const QString id = idForTarget(target);
        const int seconds = m_resolver.durationFor(id);
        if (seconds <= 0) {
            m_resolver.requestDuration(id);
            return QString();
        }
        return seconds >= 3600 ? QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, QChar('0')).arg(seconds % 60, 2, 10, QChar('0'))
                               : QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));
    };
    m_videoPage->onPick = [this](const QString &target) {
        if (m_preview) {
            playPreview(target);   // a tab's page: from its list
            return;
        }
        // The entry's place in mpv's list, which a shuffle may have moved.
        const QVariantList list = m_mpv.property("playlist").toList();
        for (int i = 0; i < list.size(); ++i)
            if (list[i].toMap().value("filename").toString() == target) {
                m_mpv.command({"playlist-play-index", i});
                m_mpv.setProperty("pause", false);
                return;
            }
    };
    m_videoPage->onListMenu = [this](const QString &target, const QString &title, const QPoint &at) {
        const QString id = idForTarget(target);
        QMenu menu(this);
        menu.addAction("Play", this, [this, target] { m_videoPage->onPick(target); });
        // Kept with the list it is in, so its tab goes on from there.
        menu.addAction("Open in new tab", this, [this, target, title, id] {
            // The row's line under the name is "channel  ·  when": the channel only.
            const QString channel = (m_videoPage->channelFor ? m_videoPage->channelFor(target) : QString())
                                        .section(QStringLiteral("  ·  "), 0, 0).trimmed();
            openInTab(VideoTab{target, id, title, channel, playingListPath(), 0});
        });
        if (!id.isEmpty())
            menu.addAction("Copy video link", this, [this, id] { copyLink(id, false); });
        menu.exec(at);
    };
    m_videoPage->onPopOut = [this] {
        m_videoPoppedOut = true;
        m_videoPage->setPoppedOut(true);
        setVideoFullScreen(false);
        schedulePlace();
        restartPlayer(QueuedPlay{playingListPath(), false, m_mpv.property("playlist-pos").toInt(), {}},
                      m_mpv.property("time-pos").toDouble());
    };
    m_videoPage->onBringBack = [this] {
        m_videoPoppedOut = false;
        m_videoPage->setPoppedOut(false);
        restartPlayer(QueuedPlay{playingListPath(), false, m_mpv.property("playlist-pos").toInt(), {}},
                      m_mpv.property("time-pos").toDouble());
    };
    m_videoPage->onFullScreen = [this] {
        // Asked for from the picture in the now playing view: by way of the
        // video page, and back where it was afterwards.
        if (!m_videoFull && m_pages->currentWidget() != m_videoPage) {
            m_fullFromPanel = true;
            showVideoPage();
        }
        setVideoFullScreen(!m_videoFull);
    };
    m_videoPage->onTogglePause = [this] { togglePause(); };
    // The timeline's pictures: the playing video's storyboard (not a tab
    // page's: there the controls are the other video's and are hidden).
    m_videoPage->previewAt = [this](double seconds) {
        const QString path = m_mpv.property("path").toString();
        QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString());
        if (id.isEmpty())
            id = youtube::videoId(path);
        const QPixmap got = !id.isEmpty() && id == m_storyboard.video() ? m_storyboard.frameAt(seconds) : QPixmap();
        return got;
    };
    m_storyboard.onReady = [this] { m_videoPage->refreshSeekPreview(); };
    // A tab's cover: Play starts what waits paused behind it.
    m_videoPage->onCoverPlay = [this] {
        if (m_preview)
            playPreview();
        else
            m_mpv.setProperty("pause", false);
    };
    // Stopped at the end (keep-open), the next or previous one plays,
    // not paused as the end left it.
    m_videoPage->onPrevious = [this] {
        m_playNextUnpaused = m_mpv.property("eof-reached").toBool();
        m_mpv.command({"playlist-prev"});
    };
    m_videoPage->onNext = [this] {
        m_playNextUnpaused = m_mpv.property("eof-reached").toBool();
        m_mpv.command({"playlist-next"});
    };
    m_videoPage->onVolume = [this](int v) { m_mpv.setProperty("volume", v); };
    m_videoPage->onToggleMute = [this] { m_mpv.command({"cycle", "mute"}); };
    // At once, not after the next turn of the event loop: the page has
    // already scrolled, and a frame with the picture still at its old place
    // read as a little jump while scrolling.
    m_videoPage->onScreenMoved = [this] { placeWithPage(); };
    // While the page scrolls, mpv's window moves in the same step as the
    // page, or the old picture is seen for a moment over the top bar.
    m_videoPage->onScrolled = [this] {
        // The page is painted where it now is first, then mpv's window is
        // moved over it, in the same step: no jump between the two.
        m_videoPage->viewport()->repaint();
        placeSurface();
    };
    m_videoPage->onMinimize = [this] {
        // Back to where the video was opened from, the video in the corner.
        m_miniAsked = true;
        setVideoFullScreen(false);
        QWidget *back = m_beforeVideo && m_beforeVideo != m_videoPage ? m_beforeVideo.data() : nullptr;
        if (back)
            m_pages->setCurrentWidget(back);
        else
            showHome();
    };
    m_videoPage->onRestore = [this] {
        // From a mini player pinned over a minimised app: the app comes back.
        if (isMinimized())
            ShowWindow(reinterpret_cast<HWND>(winId()), SW_RESTORE);
        activateWindow();
        showVideoPage();
    };
    m_videoPage->onMiniMoved = [this](const QPoint &at) {
        m_miniAt = at;
        QSettings().setValue("video/miniAt", at);
    };
    m_videoPage->onMiniResized = [this](const QSize &size) {
        // mpv's window follows the mini player's edges as they are dragged.
        if (m_videoPage->surface() && m_videoPage->miniWindow()->isVisible()) {
            m_videoPage->surface()->place(m_videoPage->miniWindow(), QRect(QPoint(0, 0), size));
            m_miniWidth = size.width();
            QSettings().setValue("video/miniWidth", m_miniWidth);
        }
    };
    if (QSettings().contains("video/miniAt"))
        m_miniAt = QSettings().value("video/miniAt").toPoint();
    m_miniWidth = QSettings().value("video/miniWidth", 0).toInt();
    m_videoPage->onCloseMini = [this] {
        m_miniClosed = true;
        m_mpv.setProperty("pause", true);
        schedulePlace();
    };
    m_videoPage->onSeek = [this](double s) { m_mpv.command({"seek", s, "absolute"}); };
    // "exact": a relative seek otherwise lands on the nearest keyframe, 12
    // or 20 seconds on instead of 10.
    m_videoPage->onSkip = [this](double s) { m_mpv.command({"seek", s, "relative+exact"}); };
    m_videoPage->onScrub = [this](double s) { m_mpv.command({"seek", s, "absolute+keyframes"}); };
    m_videoPage->onSubtitle = [this](const QVariant &sid) { m_mpv.setProperty("sid", sid); };
    m_videoPage->onSpeed = [this](double s) { m_mpv.setProperty("speed", s); };
    m_videoPage->onQuality = [this](int height) {
        // The cap for this and the next videos, this one reopened at the
        // same second (quality.lua does the same when it steps down).
        const QString format = height > 0 ? QStringLiteral("bestvideo[height<=%1]+bestaudio/best").arg(height)
                                          : QStringLiteral("bestvideo+bestaudio/best");
        m_mpv.setProperty("ytdl-format", format);
        m_mpv.setProperty("start", QString::number(m_mpv.property("time-pos").toDouble(), 'f', 1));
        armStartReset(m_mpv.property("playlist-pos").toInt());
        m_mpv.command({"playlist-play-index", m_mpv.property("playlist-pos").toInt()});
    };
    // Autoplay (Batin, 2026-11-20): off, a video stops at its end with the
    // next one offered; on, the next one plays after a count of 8.
    m_videoPage->setAutoplay(QSettings().value("video/autoplay", false).toBool());
    // Cinema or the normal view (Batin, 2026-11-21): the bar's button and T,
    // kept for the next video.
    m_videoPage->setCinema(QSettings().value("video/cinema", false).toBool());
    m_videoPage->onCinema = [this] {
        const bool on = !m_videoPage->cinema();
        QSettings().setValue("video/cinema", on);
        m_videoPage->setCinema(on);
        schedulePlace();   // mpv's window follows the player's new size
    };
    m_videoPage->onAutoplay = [this] {
        const bool on = !QSettings().value("video/autoplay", false).toBool();
        QSettings().setValue("video/autoplay", on);
        m_videoPage->setAutoplay(on);
        if (m_videoPage->upNextShown())
            offerNextVideo();   // the card shown again the new way
    };
    m_videoPage->onUpNextPlay = [this] {
        m_videoPage->hideUpNext();
        // At its end mpv paused it (keep-open), and unpaused there it paused
        // again at once, still at the end: the next one is unpaused once it
        // has begun (seen, 2026-11-20).
        m_playNextUnpaused = true;
        m_mpv.command({"playlist-next"});
    };
    m_videoPage->onRepeat = [this] {
        m_mpv.setProperty("loop-file", mpvLooping(m_mpv.property("loop-file")) ? "no" : "inf");
    };
    m_videoPage->onListenOnly = [this](bool on) {
        // ghost.lua opens this and the next videos with no picture and a
        // sound-only format while it is set; this one reopens at the same
        // second, as a quality change does.
        m_videoPage->setListenOnly(on);
        m_mpv.setProperty("user-data/ghost/listen-only", on);
        // Turned on: a new mpv at the same second that never opens a
        // picture. The same one kept what drawing the picture had taken,
        // ~200 MB, where a song's mpv uses ~50 (Batin, 2026-11-20).
        const QString list = playingListPath();
        if (on && m_mpv.connected() && m_mpvEmbedded && !list.isEmpty() && QFileInfo::exists(list)) {
            restartPlayer(QueuedPlay{list, false, m_mpv.property("playlist-pos").toInt(), {}},
                          m_mpv.property("time-pos").toDouble());
            schedulePlace();
            return;
        }
        if (m_mpv.connected()) {
            m_mpv.setProperty("start", QString::number(m_mpv.property("time-pos").toDouble(), 'f', 1));
            armStartReset(m_mpv.property("playlist-pos").toInt());
            m_mpv.command({"playlist-play-index", m_mpv.property("playlist-pos").toInt()});
        }
        schedulePlace();
    };
    // The video page's keys (arrows, F, I) are shortcuts now: see
    // runShortcut and Settings > Keyboard shortcuts.
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this] { if (m_videoFull) setVideoFullScreen(false); });
    m_lyricsView->onSeek = [this](double seconds) { m_mpv.command({"seek", seconds, "absolute"}); };
    connect(&m_lyricsSource, &LyricsSource::ready, this, [this](const QString &key, const Lyrics &lyrics) {
        if (key == m_lyricsKey) {
            m_lyricsView->setLyrics(lyrics, m_mpv.property("media-title").toString());
            m_lyrics = lyrics;
            m_panel->setLyrics(lyrics, false);
            m_panel->setPosition(m_mpv.property("time-pos").toDouble());
        }
    });

    auto *mainPanel = new QWidget;
    mainPanel->setObjectName("panel");
    mainPanel->setAttribute(Qt::WA_StyledBackground);
    m_mainPanel = mainPanel;
    theme::whenChanged(mainPanel, [this] { applyLayoutColors(); });
    auto *mainBox = new QVBoxLayout(mainPanel);
    mainBox->setContentsMargins(0, 0, 0, 0);
    mainBox->setSpacing(0);
    mainBox->addWidget(m_pages, 1);
    // Under a list's page, its Recommended sheet, on the page card itself;
    // a soft shadow falls up from its top edge over the songs.
    mainBox->addWidget(m_recCard);
    m_recShade = new tiles::Shade(mainPanel, false);
    m_recCard->installEventFilter(this);
    connect(m_pages, &QStackedWidget::currentChanged, this, [this] { updateRecCard(); });

    // The now playing view, Spotify's, at the right of the page: a panel of
    // its own, its width the user's.
    m_panel = new NowPlayingPanel;
    m_rightSplit = new QSplitter(Qt::Horizontal);
    m_rightSplit->addWidget(mainPanel);
    m_rightSplit->addWidget(m_panel);
    m_rightSplit->setStretchFactor(0, 1);
    m_rightSplit->setStretchFactor(1, 0);
    m_rightSplit->handle(1)->installEventFilter(this);
    m_rightSplit->setHandleWidth(8);
    m_rightSplit->setChildrenCollapsible(false);
    m_rightSplit->setSizes({940, 340});
    m_panelOpen = QSettings().value("panel/open", true).toBool();
    m_panel->onClose = [this] {
        m_panelOpen = false;
        QSettings().setValue("panel/open", false);
        updatePanelVisibility();
    };
    m_panel->onSwitchVideo = [this] { setSongVideo(!(songVideo() && !panelVideo())); };
    m_panel->onMiniVideo = [this] { setPanelVideo(!panelVideo()); };
    m_panel->coverArea()->installEventFilter(this);
    if (auto *scroll = qobject_cast<QScrollArea *>(m_panel->scrollArea()))
        connect(scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::placeWithPage);
    m_panel->onOpenLyrics = [this] {
        if (m_pages->currentWidget() != m_lyricsView)
            toggleLyrics();
    };
    m_panel->onArtist = [this] {
        if (openPlayingChannel())
            return;
        const QString artist = playingSong().first;
        goToArtist(artist.isEmpty() ? m_mpv.property("metadata/by-key/uploader").toString() : artist);
    };
    m_panel->onAbout = [this] {
        const QString source = m_mpv.property("user-data/ghost/source").toString();
        const auto [artist, track] = playingSong();
        showSongInfo(source.isEmpty() ? m_mpv.property("path").toString() : source,
                     track.isEmpty() ? m_mpv.property("media-title").toString() : track, artist);
    };
    m_panel->onPlayNext = [this] { m_mpv.command({"playlist-next"}); };
    m_panel->onFollow = [this] {
        setFollowing(m_panelChannelId, m_panelChannelName, !follows(m_panelChannelId));
    };
    connect(&m_artists, &ArtistLookup::ready, this, [this](const QString &channel) {
        if (channel == m_panelChannel)
            m_panel->setArtist(m_artists.known(channel));
        if (m_pages->currentWidget() == m_profileScroll)
            m_profileSoon.start();
    });
    m_profileSoon.setSingleShot(true);
    m_profileSoon.setInterval(150);
    connect(&m_profileSoon, &QTimer::timeout, this, &MainWindow::refreshProfile);
    connect(&m_profile, &DiscordProfile::changed, this, &MainWindow::refreshProfileUser);
    m_profile.refresh();
    QTimer::singleShot(0, this, &MainWindow::refreshProfileUser);
    m_panelSoon.setSingleShot(true);
    m_panelSoon.setInterval(150);
    connect(&m_panelSoon, &QTimer::timeout, this, &MainWindow::refreshPanel);
    m_videoPage->onSwitchToAudio = [this] { setSongVideo(false); };
    m_videoPage->onFollow = [this] {
        const QString name = m_mpv.property("metadata/by-key/uploader").toString();
        setFollowing(m_videoChannelId, name, !follows(m_videoChannelId));
    };
    m_videoPage->onOpenChannel = [this] { openPlayingChannel(true); };

    m_split = new QSplitter(Qt::Horizontal);
    m_leftStack = new QStackedWidget;
    m_leftStack->addWidget(buildLibrary());
    m_leftStack->addWidget(buildYouTubeSidebar());
    m_split->addWidget(m_leftStack);
    m_split->addWidget(m_rightSplit);
    m_split->setStretchFactor(1, 1);
    m_split->setHandleWidth(8);
    m_split->setChildrenCollapsible(false);
    m_split->setSizes({300, 940});
    m_split->handle(1)->installEventFilter(this);
    connect(m_split, &QSplitter::splitterMoved, this, [this] {
        setLibraryCompact(m_split->sizes().value(0) < compactBelow());
        syncSearchWidth();
        updatePanelVisibility();
        m_splitMoved.start();
        // The controls over the picture follow the edge at once, from the
        // page as it is laid out now.
        startSurfaceDrag();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        placeSurface();
        // And painted now where the picture was: left for later, a strip of
        // what stood there before stayed for a moment.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
    });
    connect(m_rightSplit, &QSplitter::splitterMoved, this, [this] {
        m_splitMoved.start();
        startSurfaceDrag();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        placeSurface();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
    });
    m_surfaceDragEnd.setSingleShot(true);
    m_surfaceDragEnd.setInterval(180);
    connect(&m_surfaceDragEnd, &QTimer::timeout, this, [this] {
        if (QApplication::mouseButtons() & Qt::LeftButton) {
            m_surfaceDragEnd.start();   // held still, not let go yet
            return;
        }
        m_surfaceDrag = false;
        placeSurface();
        m_panelSoon.start();   // the cover back from the held frame
    });

    m_nowPlaying = new NowPlayingBar(&m_mpv, &m_covers);
    m_nowPlaying->onResume = [this] { resume(); };
    m_markSoon.setSingleShot(true);
    m_markSoon.setInterval(250);
    connect(&m_markSoon, &QTimer::timeout, this, &MainWindow::markPlaying);
    m_resumeSave.setInterval(10000);
    connect(&m_resumeSave, &QTimer::timeout, this, &MainWindow::saveResume);
    m_resumeSave.start();
    connect(&m_mpv, &MpvClient::connectionChanged, this, [this](bool on) {
        if (!on)
            showResume();
    });
    QTimer::singleShot(0, this, &MainWindow::showResume);
    // After an update, once: what the new version brings.
    // Or, its own update having stopped short, the way on (1.6).
    QTimer::singleShot(1500, this, [this] {
        if (UpdateDialog::showIfUnfinished(this)) {
            showUpdates();
            return;
        }
        WhatsNewDialog::showIfUpdated(this, [](QAbstractScrollArea *area) { new SmoothScroll(area); });
    });
    m_settings->onLookChanged = [this] { applyLook(); };
    m_settings->onSearchPlaceChanged = [this](bool middle) { setSearchMiddle(middle); };
    m_settings->onUsageChanged = [this] { applyUsage(); };
    m_settings->onIslandChanged = [this] { m_island->refresh(); };
    m_settings->onTabsChanged = [this] { tabsChanged(); };
    m_settings->onRegionChanged = [this] {
        m_searchPage->setRegion(ytmusic::searchRegion());
        m_ytClient.setRegion(ytmusic::searchRegion());
    };
    m_settings->onResumeChanged = [this] { showResume(); };
    m_settings->onNormalizeChanged = [this](bool) { applyNormalize(); };
    m_settings->onEqualizerChanged = [this](int band) { applyEqualizer(band); };
    m_settings->onHistoryChanged = [this](bool on) {
        m_stats.setKeepHistory(on);
        refreshProfile();
    };
    m_settings->onClearHistory = [this] {
        m_stats.clearHistory();
        refreshProfile();
        toast("History cleared");
    };
    m_settings->onZoomChanged = [this](qreal zoom) { changeZoom(zoom - theme::zoom()); };
    // Settings > Performance: mpv takes the new cache at once (the file
    // playing keeps what it has loaded), the covers' budget too.
    const auto applyMemory = [this](int index) {
        const performance::Level &l = performance::level(index);
        m_covers.setMemoryBudget(l.coversMb);
        if (!m_mpv.connected())
            return;
        m_mpv.setProperty("demuxer-max-bytes", QString::fromLatin1(l.ahead) + "iB");
        m_mpv.setProperty("demuxer-max-back-bytes", QString::fromLatin1(l.back) + "iB");
        m_mpv.setProperty("demuxer-readahead-secs", l.aheadSecs);
    };
    m_settings->onMemoryChanged = applyMemory;
    m_covers.setMemoryBudget(performance::level(QSettings().value("performance/memory", performance::kDefault).toInt()).coversMb);
    m_settings->memoryUsage = [this] { return memoryUsage(); };
    // cookies.lua reads it on the next retry, so a running mpv is told too.
    m_settings->onCookiesBrowserChanged = [this](const QString &browser) {
        m_mpv.command({"change-list", "script-opts", "append", "ghost-cookies-browser=" + browser});
    };
    // rpc_exporter.lua reads these each time it writes the bridge file and
    // tells ghost_rpc.py to show nothing for what is turned off.
    const auto applyPresence = [this] {
        if (!m_mpv.connected())
            return;
        QSettings settings;
        m_mpv.setProperty("user-data/ghost/rpc-songs", !m_private && settings.value("presence/songs", true).toBool());
        m_mpv.setProperty("user-data/ghost/rpc-videos", !m_private && settings.value("presence/videos", true).toBool());
    };
    m_applyPresence = applyPresence;
    m_settings->onPrivateChanged = [this](bool on) { setPrivateSession(on); };
    m_settings->onPresenceChanged = [applyPresence](bool, bool) { applyPresence(); };
    connect(&m_mpv, &MpvClient::connectionChanged, this, [applyPresence](bool on) { if (on) applyPresence(); });
    m_settings->downloadsSummary = [this] {
        const int n = m_offline.songCount();
        return n == 0 ? QStringLiteral("Nothing downloaded yet.")
                      : QStringLiteral("%1 %2, %3 MB").arg(n).arg(n == 1 ? "song" : "songs")
                            .arg(double(m_offline.bytesUsed()) / (1024 * 1024), 0, 'f', 1);
    };
    m_settings->onRemoveDownloads = [this] { m_offline.removeAll(); };
    m_settings->onShowShortcuts = [this] { showShortcuts(); };
    // To a player already open too: its next video asks yt-dlp for these
    // and shows the first it has.
    m_settings->onSubtitlesChanged = [this](bool on, const QStringList &languages) {
        if (!m_mpv.connected())
            return;
        m_mpv.command({"change-list", "script-opts", "append", "ghost-sub-langs=" + languages.join(',')});
        m_mpv.setProperty("slang", on ? languages.join(',') : QString());
    };
    m_settings->downloadedLists = [this] {
        QList<SettingsPage::DownloadedList> out;
        const auto &all = m_library.playlists();
        for (int i = 0; i < all.size(); ++i) {
            if (!m_offline.isMarked(all[i].path))
                continue;
            const QStringList targets = targetsOf(all[i]);
            QPixmap cover(72, 72);   // drawn at 2x for a 36 px square
            cover.fill(Qt::transparent);
            QPainter p(&cover);
            tiles::paintCollage(p, QRectF(0, 0, 72, 72), coversFor(i), all[i].name, 12);
            p.end();
            cover.setDevicePixelRatio(2);
            out.append({all[i].path, all[i].name, cover, m_offline.filesFor(targets),
                        m_offline.bytesFor(targets), m_offline.pending(all[i].path)});
        }
        return out;
    };
    m_settings->onRemoveListDownload = [this](const QString &path) {
        const auto &all = m_library.playlists();
        for (int i = 0; i < all.size(); ++i)
            if (all[i].path == path) {
                m_offline.remove(path, targetsOf(all[i]), [this](const QString &file) {
                    for (const Playlist &other : m_library.playlists())
                        if (QFileInfo(other.path).fileName() == file)
                            return targetsOf(other);
                    return QStringList();
                });
                return;
            }
    };
    m_settings->onVideoFormatChanged = [this](const QString &format) {
        // From the next video. While a song list plays, ghost.lua is told
        // instead: it puts this format back when a video list opens.
        if (!m_mpv.connected())
            return;
        if (m_mpv.property("user-data/ghost/audio").toBool())
            m_mpv.command({"change-list", "script-opts", "append", QStringLiteral("ghost-video-format=") + format});
        else
            m_mpv.setProperty("ytdl-format", format);
    };
    m_settings->onPlayerWindowChanged = [this](bool shown) {
        sendWindowChoice();
        // A song list playing now shows or hides its window at once.
        if (m_mpv.connected() && m_mpv.property("user-data/ghost/audio").toBool())
            m_mpv.setProperty("force-window", shown ? "yes" : "no");
    };
    m_nowPlaying->canLike = [this] {
        return m_mpv.connected() && !playingTarget().isEmpty() && m_mpv.property("user-data/ghost/audio").toBool();
    };
    m_nowPlaying->isLiked = [this] {
        const QString path = likedPath(false);
        const QString id = youtube::videoId(playingTarget());
        if (path.isEmpty() || id.isEmpty())
            return false;
        for (const PlaylistEntry &e : Playlist::peek(path).readEntries())
            if (youtube::videoId(e.target) == id)
                return true;
        return false;
    };
    m_nowPlaying->onLike = [this] { toggleLiked(); };
    m_nowPlaying->onSleepMenu = [this](const QPoint &global) { sleepMenu(global); };
    m_sleep.setSingleShot(true);
    connect(&m_sleep, &QTimer::timeout, this, [this] {
        m_mpv.setProperty("pause", true);
        toast("Sleep timer: paused");
    });
    // "End of this song": paused as the next one starts.
    connect(&m_mpv, &MpvClient::propertyChanged, this, [this](const QString &name) {
        if (!m_sleepAtSongEnd || (name != "path" && name != "playlist-pos"))
            return;
        const QString now = m_mpv.property("path").toString() + QString::number(m_mpv.property("playlist-pos").toInt());
        if (now == m_sleepSong)
            return;
        m_sleepAtSongEnd = false;
        m_mpv.setProperty("pause", true);
        toast("Sleep timer: paused");
    });
    m_nowPlaying->onArtist = [this] {
        // No mpv yet (the app just opened, the bar waiting where it left
        // off): the artist kept with that song.
        if (!m_mpv.connected()) {
            goToArtist(QSettings().value("resume/artist").toString());
            return;
        }
        if (openPlayingChannel())
            return;
        const QString artist = playingSong().first;
        goToArtist(artist.isEmpty() ? m_mpv.property("metadata/by-key/uploader").toString() : artist);
    };
    m_searchPage->onBack = [this] { goHistory(false); };
    m_nowPlaying->onLikeMenu = [this](const QPoint &global) {
        const auto [artist, track] = playingSong();
        likeMenu(playingTarget(), track.isEmpty() ? m_mpv.property("media-title").toString().trimmed()
                                                  : (artist.isEmpty() ? track : artist + QStringLiteral(" - ") + track),
                 global);
    };
    m_nowPlaying->positionText = [this] { return positionText(); };
    m_nowPlaying->numberFor = [this] {
        // The number the song has where it is shown: its row in the open
        // list when that is the one playing, else its place in the file.
        if (m_playingRow >= 0)
            return m_songs->item(m_playingRow)->data(tiles::kNumberRole).toInt();
        const QString list = playingListPath();
        const QString path = m_mpv.property("path").toString();
        if (list.isEmpty() || path.isEmpty())
            return 0;
        if (list != m_numberedList) {
            m_numberedList = list;
            m_numberedEntries.clear();
            Playlist p;
            p.path = list;
            for (const PlaylistEntry &e : p.readEntries())
                m_numberedEntries << e.target;
        }
        return int(m_numberedEntries.indexOf(path)) + 1;
    };
    connect(m_nowPlaying, &NowPlayingBar::openPlaying, this, &MainWindow::openPlayingList);
    connect(m_nowPlaying, &NowPlayingBar::toggleLyrics, this, &MainWindow::toggleLyrics);
    connect(m_nowPlaying, &NowPlayingBar::togglePanel, this, [this] {
        m_panelOpen = !m_panelOpen;
        QSettings().setValue("panel/open", m_panelOpen);
        updatePanelVisibility();
        if (m_panelOpen)
            refreshPanel();
        if (m_panelOpen && !panelFits())
            toast("Make the window wider to see the now playing view");
    });

    auto *root = new QVBoxLayout;
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildTopBar());
    // The same gap on every side of the two panels.
    auto *body = new QHBoxLayout;
    body->setContentsMargins(8, 0, 8, 0);   // and 8 above and below on Light: see applyLayoutColors
    m_bodyBox = body;
    body->addWidget(m_split);
    root->addLayout(body, 1);
    root->addWidget(m_nowPlaying);
    auto *central = new QWidget;
    central->setObjectName("central");
    central->setLayout(root);
    setCentralWidget(central);
    QTimer::singleShot(0, this, [this] { applyLayoutColors(); });

    // The island: a YouTube video playing whose page is not open.
    m_island = new NowIsland(&m_mpv, &m_covers, this);
    m_island->shouldShow = [this] {
        // On both sides, for a song as for a video (Batın, 2026-11-12); a
        // video's not over its own page or full screen. Off in Settings.
        if (!QSettings().value("look/island", true).toBool() || !m_mpv.connected())
            return false;
        if (m_mpv.property("user-data/ghost/audio").toBool())
            return true;
        return !m_videoFull && (m_pages->currentWidget() != m_videoPage || m_preview);
    };
    m_island->freeSpan = [this] {
        // From the last thing on the left (the search box, or its button) to
        // the first on the right (Update, the profile).
        // Not from the tabs: they keep to the island's left, so it stays in
        // the middle (Batin, 2026-11-20).
        QWidget *leftmost = m_search->isVisible() ? static_cast<QWidget *>(m_search) : m_searchButton;
        QWidget *rightmost = m_updateButton->isVisible() ? m_updateButton
                           : m_profileButton->isVisible() ? m_profileButton : m_gearButton;
        int left = leftmost->mapTo(this, QPoint(leftmost->width(), 0)).x();
        const int right = rightmost->mapTo(this, QPoint(0, 0)).x();
        // A narrow window: the tabs (folded) reach past the island's half,
        // so it starts after them and narrows rather than covering them.
        if (m_tabStrip && m_tabStrip->isVisible()) {
            const int tabsRight = m_tabStrip->mapTo(this, QPoint(m_tabStrip->width(), 0)).x() + 8;
            if (tabsRight > (left + right) / 2 - qRound(235 * theme::chromeZoom()))
                left = tabsRight;
        }
        return QPair<int, int>(left, right);
    };
    m_island->onStop = [this] {
        // The card's X: what plays is closed, as closing mpv's window does.
        endPreview(false);
        if (m_mpv.connected()) {
            m_mpv.command({"quit"});
            m_mpv.flush();
        }
        if (m_pages->currentWidget() == m_videoPage)
            m_pages->setCurrentWidget(m_beforeVideo ? m_beforeVideo.data() : m_pages->widget(0));
    };
    m_island->onOpen = [this] {
        endPreview();
        // A song from the YouTube side: its list is on the music side.
        if (m_youtubeLayout && m_mpv.property("user-data/ghost/audio").toBool() && !songVideo())
            setYouTubeLayout(false, true, [this] { openPlayingList(); });
        else
            openPlayingList();
    };
    m_island->onPlayPause = [this] { togglePause(); };
    m_island->onMini = [this] {
        m_miniAsked = true;
        m_pictureAway = false;
        m_miniClosed = false;
        placeSurface();
    };
    m_chromeButtons << m_island;   // clicked, not a place to drag the window by

    // A stream on air, opened from its master playlist (livesync.lua): the
    // timeline is the last hour or so and its end is what is on air now.
    // Behind = more than 12 s from that end; "Go live" goes to it on the
    // timeline (reopening the stream, as first done, left it where it was
    // after a click on the timeline: Batın, 2026-11-09).
    const auto showLive = [this] {
        m_videoPage->setLive(m_mpv.property("user-data/ghost/live").toBool(), m_liveBehind);
        showVideoChannel();   // LIVE on its picture
    };
    connect(&m_pictures, &RemotePictures::ready, this, [this](const QString &url) {
        if (!m_videoChannelId.isEmpty() && url == m_channelAvatars.value(m_videoChannelId))
            showVideoChannel();
    });
    // Live, measured in the app (2026-11-12): it plays steadily some 16-20 s
    // from the end of the timeline, and right at the end it waits for each
    // new segment and falls back. That is how the HLS demuxer keeps the end
    // of the list: playing at 1.05x for 100 s did not bring it closer (it
    // only played the music fast), so the speed is left alone. "LIVE" up to
    // 25 s from the end; "Go live" takes it 12 s from the end.
    connect(&m_mpv, &MpvClient::propertyChanged, this, [this, showLive](const QString &name) {
        const bool live = m_mpv.property("user-data/ghost/live").toBool();
        if (name == "user-data/ghost/live") {
            m_liveBehind = false;
            showLive();
        } else if (live && (name == "time-pos" || name == "duration" || name == "pause")) {
            const double at = m_mpv.property("time-pos").toDouble();
            const double end = m_mpv.property("duration").toDouble();
            const bool behind = end > 0 && end - at > kLiveBehind;
            if (behind != m_liveBehind) {
                m_liveBehind = behind;
                showLive();
            }
        }
    });
    m_videoPage->onGoLive = [this, showLive] {
        const double end = m_mpv.property("duration").toDouble();
        m_mpv.setProperty("pause", false);
        // Not the very end: there it waits for each new segment and falls
        // back to where the next one is anyway (measured, 2026-11-12).
        if (end > 0)
            m_mpv.command({"seek", qMax(0.0, end - kLiveGoTo), "absolute"});
        else
            m_mpv.command({"playlist-play-index", "current"});
        m_liveBehind = false;
        showLive();
    };
    connect(m_pages, &QStackedWidget::currentChanged, m_island, [this] { m_island->refresh(); });

    // mpv's window lives over the whole window, not in the page, so it can
    // also sit in a corner while another page is open.
    m_videoPage->attachSurface(central);
    m_videoPage->setOwner(this);
    m_placeTimer = new QTimer(this);
    m_placeTimer->setSingleShot(true);
    m_placeTimer->setInterval(0);
    connect(m_placeTimer, &QTimer::timeout, this, &MainWindow::placeSurface);
    m_videoMouse = new QTimer(this);
    m_videoMouse->setInterval(200);
    connect(m_videoMouse, &QTimer::timeout, this, &MainWindow::watchVideoMouse);
    m_videoMouse->start();
    connect(m_pages, &QStackedWidget::currentChanged, this, &MainWindow::schedulePlace);
    connect(m_pages, &QStackedWidget::currentChanged, this, &MainWindow::updatePanelVisibility);
    connect(&m_mpv, &MpvClient::connectionChanged, this, [this] { m_panelSoon.start(); });
    connect(&m_mpv, &MpvClient::connectionChanged, this, [this](bool on) {
        if (on) {
            applyNormalize();
            applyEqualizer(-1);
        }
    });

    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    m_split->restoreState(settings.value("window/split").toByteArray());
    setSearchMiddle(settings.value("search/middle", false).toBool());
    // A theme, accent or zoom put the YouTube menu's buttons back to their
    // first style (names left-aligned): folded, its icons stood off centre
    // until the window was resized (Batin, 2026-11-20).
    theme::whenChanged(this, [this] {
        QTimer::singleShot(0, this, [this] {
            if (!m_ytSidebar)
                return;
            setYtCompact(m_ytCompact);
            m_ytChannelPad = -1;
            padYtChannels();
        });
    });
    m_visitSoon.setSingleShot(true);
    m_visitSoon.setInterval(0);
    connect(&m_visitSoon, &QTimer::timeout, this, &MainWindow::noteVisit);
    connect(m_pages, &QStackedWidget::currentChanged, this, [this] {
        m_visitSoon.start();
        if (m_preview && m_pages->currentWidget() != m_videoPage)
            endPreview();
    });
    // The YouTube menu's row (Home, History...) lit only while its page is
    // what shows: on a video it read as still being on Home (Batin, 2026-11-20).
    connect(m_pages, &QStackedWidget::currentChanged, this, [this] {
        const bool there = m_pages->currentWidget() == m_searchPage;
        for (int i = 0; i < m_ytNav.size(); ++i)
            m_ytNav[i]->setChecked(there && i == m_ytMark);
        if (m_ytSearchNav)
            m_ytSearchNav->setChecked(there && m_ytMark == kYtSearch);
    });
    m_searchPage->onViewChanged = [this] { m_visitSoon.start(); };
    if (settings.value("layout/youtube", false).toBool())
        QTimer::singleShot(0, this, [this] { setYouTubeLayout(true, false); });
    QTimer::singleShot(0, this, [this] { applyUsage(); });
    m_rightSplit->restoreState(settings.value("window/panelSplit").toByteArray());
    setLibraryCompact(m_split->sizes().value(0) < compactBelow());

    m_resolver.setYtDlp(QFileInfo(findMpv()).absoluteDir().filePath("yt-dlp.exe"));
    m_offline.setYtDlp(QFileInfo(findMpv()).absoluteDir().filePath("yt-dlp.exe"));
    // The same video mpv would play: the entry's own, songmatch's match for
    // a search, the one found for its cover, else the search itself.
    m_offline.sourceFor = [this](const QString &target) -> QString {
        QString id = youtube::videoId(target);
        if (id.isEmpty()) {
            const QString query = youtube::searchQuery(target);
            if (query.isEmpty())
                return {};
            id = m_matches.idFor(query);
            if (id.isEmpty())
                id = m_resolver.idFor(query);
            if (id.isEmpty())
                return QStringLiteral("ytsearch1:") + query;
        }
        return QStringLiteral("https://www.youtube.com/watch?v=") + id;
    };
    connect(&m_offline, &OfflineStore::changed, this, [this](const QString &list) {
        // Marks on the open list's songs, and its button.
        const auto &all = m_library.playlists();
        if (m_open >= 0 && (list.isEmpty() || all[m_open].path == list))
            for (int row = 0; row < m_songs->count(); ++row)
                m_songs->item(row)->setData(tiles::kOfflineRole, m_offline.has(m_songs->item(row)->data(Qt::UserRole).toString()));
        showDownloadState();
        if (m_pages->currentWidget() == m_settings)
            m_settings->load();
    });
    auto repaint = [this] {
        for (QListWidget *v : {m_libraryList, m_recentAdded, m_recentPlayed, m_topLists, m_songs})
            v->viewport()->update();
        m_playlistCover->update();
        m_nowPlaying->refreshCover();
        tintLyrics();
        m_videoPage->refreshCovers();
    };
    connect(&m_resolver, &CoverResolver::resolved, this, [this, repaint] {
        fillDurations();
        repaint();
    });
    connect(&m_covers, &ThumbnailCache::ready, this, repaint);
    connect(&m_covers, &ThumbnailCache::ready, this, [this] {
        if (m_panel->isVisible())
            m_panelSoon.start();
        if (m_pages->currentWidget() == m_profileScroll)
            m_profileSoon.start();
    });
    connect(&m_mpv, &MpvClient::propertyChanged, this, [this, repaint](const QString &name) {
        if (name == "media-title" || name == "pause")
            updateTray();
        // A song played since the last look may have given a list its cover.
        if (name == "user-data/ghost/source")
            QTimer::singleShot(500, repaint);
        if (name == "playlist-pos" || name == "playlist-path" || name == "path")
            markPlaying();
        // mpv sends a new song's place, path and list one by one; marked
        // between them, a queued song's row stayed lit after the list came
        // back (seen with a list page open, right after reopening). Once
        // they have all come, the marks are made again.
        if (name == "playlist-pos" || name == "playlist-path" || name == "path" || name == "playlist")
            m_markSoon.start();
        if (name == "playlist" || name == "playlist-pos")
            syncQueue();
        if (name == "loop-file")
            m_videoPage->setRepeat(mpvLooping(m_mpv.property("loop-file")));
        // A video stops at its end rather than going on by itself; songs go
        // on as ever. Only inside the app (mpv's own window keeps mpv's ways).
        if (name == "pid")
            m_keepOpen.clear();   // a new mpv: told again
        if (name == "user-data/ghost/audio" || name == "path" || name == "pid") {
            const bool video = m_mpvEmbedded && !m_mpv.property("user-data/ghost/audio").toBool();
            const QString want = video ? QStringLiteral("always") : QStringLiteral("no");
            if (want != m_keepOpen) {
                m_keepOpen = want;
                m_mpv.setProperty("keep-open", want);
            }
        }
        if (name == "path" && m_playNextUnpaused && !m_mpv.property("path").toString().isEmpty()) {
            m_playNextUnpaused = false;
            m_mpv.setProperty("pause", false);
        }
        if (name == "eof-reached" || name == "path" || name == "playlist-pos") {
            if (name == "eof-reached" && m_mpv.property("eof-reached").toBool())
                offerNextVideo();
            else if (m_videoPage->upNextShown())
                m_videoPage->hideUpNext();
            schedulePlace();
        }
        if (name == "playlist-pos" || name == "playlist-count" || name == "loop-playlist" || name == "loop-file")
            maybeAutoplay();
        if (name == "playlist-count" || name == "playlist-path" || name == "path")
            applyPendingOrder();
        // A song's lyrics are looked up once its length is known, which
        // tells versions of it apart.
        if (name == "duration")
            lookUpLyrics();
        if (name == "time-pos" && m_pages->currentWidget() == m_lyricsView)
            m_lyricsView->setPosition(m_mpv.property("time-pos").toDouble());
        if (name == "user-data/ghost/source" || name == "path")
            tintLyrics();
        if (name == "user-data/ghost/source" && !songVideo()) {
            const QString query = youtube::searchQuery(m_mpv.property("path").toString());
            const QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString());
            if (!query.isEmpty() && !id.isEmpty())
                m_matches.remember(query, id);
        }
        if (name == "path" || name == "media-title" || name == "metadata/by-key/uploader" ||
            name == "playlist-path" || name == "user-data/ghost/source")
            updateVideoPage();
        // YouTube's volume is its own (Batin, 2026-11-20): the level a video
        // was left at is where the next one plays, and music keeps its own;
        // switching between them puts each back.
        if (name == "volume" && m_mpv.connected() && !m_mpv.property("path").toString().isEmpty())
            QSettings().setValue(m_mpv.property("user-data/ghost/audio").toBool() ? "music/volume" : "youtube/volume",
                                 m_mpv.property("volume").toDouble());
        if (name == "user-data/ghost/audio" && m_mpv.connected()) {
            // A video always at YouTube's level; songs at theirs only when
            // coming back from a video in the same mpv (a new one starts at
            // Settings > Starting volume, which is the music side's).
            const bool audio = m_mpv.property("user-data/ghost/audio").toBool();
            const QVariant want = QSettings().value(audio ? "music/volume" : "youtube/volume");
            if (want.isValid() && (!audio || m_lastKindVideo) &&
                qAbs(want.toDouble() - m_mpv.property("volume").toDouble()) > 0.5)
                m_mpv.setProperty("volume", want.toDouble());
            m_lastKindVideo = !audio;
        }
        // The video's shape known: a narrow one keeps it in the 16:9 frame.
        if (name == "user-data/ghost/audio" || name == "video-params/aspect")
            schedulePlace();
        if (name == "path" || name == "user-data/ghost/source" || name == "media-title" || name == "playlist-pos" ||
            name == "playlist" || name == "playlist-path" || name == "user-data/ghost/audio" ||
            name == "user-data/ghost/video-on" || name == "metadata/by-key/uploader" ||
            name == "user-data/ghost/video-panel" || name == "video-params/aspect")
            m_panelSoon.start();
        if (name == "time-pos" && m_panel->isVisible())
            m_panel->setPosition(m_mpv.property("time-pos").toDouble());
        if (name == "time-pos")
            trackListening();
        // A tab's cover goes once it plays, or once another video comes.
        if (m_videoPage->coverShown()) {
            const QString path = m_mpv.property("path").toString();
            // A tab's page (preview) keeps it whatever the other video does.
            bool off = !m_preview && name == "pause" && !m_mpv.property("pause").toBool() && m_mpv.connected();
            if (!m_preview && name == "path" && !path.isEmpty() && path == m_coverTarget)
                off = true;   // the picked video is what mpv has now
            if (off) {
                m_videoPage->setCover(false);
                m_coverTarget.clear();
                schedulePlace();
            }
        }
        // Where each video was left, for Back to it.
        // Not while a file changes: time-pos is gone for a moment then, and
        // read as 0 it wrote over the second the video was left at.
        const QVariant timePos = name == "time-pos" ? m_mpv.property("time-pos") : QVariant();
        const bool timeKnown = timePos.isValid() && !timePos.isNull() && timePos.toDouble() > 0.5;
        // Left in its last seconds, a video is kept as watched: it opens
        // from its start again, as on YouTube (it ended at once otherwise).
        const double runs = m_mpv.property("duration").toDouble();
        const double kept = timeKnown && runs > 30 && timePos.toDouble() > runs - 10 ? 0.0 : timePos.toDouble();
        if (timeKnown && m_mpv.property("path").toString() == m_lastPath)
            m_lastPos = kept;
        if (!m_frameWaitPath.isEmpty() && name == "time-pos" && timePos.isValid() && !timePos.isNull() &&
            m_mpv.property("path").toString() == m_frameWaitPath && m_mpv.property("video-params/aspect").isValid()) {
            m_frameWaitPath.clear();   // its first frame is up
            schedulePlace();
        }
        if (name == "path") {
            const QString path = m_mpv.property("path").toString();
            // Another video in the app: its thumbnail until its first frame.
            if (!path.isEmpty() && path != m_lastPath && m_mpvEmbedded && !m_videoPage->listenOnly() &&
                !m_mpv.property("user-data/ghost/audio").toBool() && !path.endsWith(QLatin1String(".m3u"), Qt::CaseInsensitive)) {
                m_frameWaitPath = path;
                const QString id = youtube::videoId(path);
                m_videoPage->setStill(id.isEmpty() ? QPixmap() : m_covers.get(id, ThumbnailCache::Size::Hero));
                QTimer::singleShot(8000, this, [this, path] {
                    if (m_frameWaitPath == path) {
                        m_frameWaitPath.clear();
                        schedulePlace();
                    }
                });
                schedulePlace();
            }
            if (!path.isEmpty() && path != m_lastPath) {
                if (!m_lastPath.isEmpty())
                    m_videoSeconds.insert(m_lastPath, m_lastPos);
                m_lastPath = path;
                m_lastPos = 0;
                // Another video on the video page is another place.
                if (m_pages->currentWidget() == m_videoPage)
                    m_visitSoon.start();
            }
        }
        // The tab that plays keeps its second, for when it is left.
        if (timeKnown && m_tabStrip->current >= 0) {
            VideoTab &tab = m_tabStrip->tabs[m_tabStrip->current];
            if (tab.target == m_mpv.property("path").toString())
                tab.seconds = kept;
        }
        if (name == "path" || name == "user-data/ghost/audio" || name == "media-title" ||
            name == "user-data/ghost/source" || name == "metadata/by-key/uploader")
            m_tabsSoon.start();
        if (m_pages->currentWidget() == m_profileScroll) {
            if (name == "path" || name == "user-data/ghost/source" || name == "media-title" || name == "pause" ||
                name == "playlist-pos" || name == "playlist-path")
                m_profileSoon.start();
            if (name == "time-pos" || name == "duration")
                m_profilePage->setPosition(m_mpv.property("time-pos").toDouble(), m_mpv.property("duration").toDouble());
        }
        if (name == "user-data/ghost/video-on")
            songVideoChanged();
        if (name == "path" && m_findingMini && m_mpv.property("path").toString() != m_miniPath) {
            m_findingMini = false;
            m_panelSoon.start();
        }
        if (name == "metadata/by-key/uploader" && !m_mpv.property("user-data/ghost/audio").toBool()) {
            const QString source = m_mpv.property("user-data/ghost/source").toString();
            m_resolver.rememberChannel(youtube::videoId(source.isEmpty() ? m_mpv.property("path").toString() : source),
                                       m_mpv.property("metadata/by-key/uploader").toString());
        }
        // Over the picture and on the mini player, whichever page is open.
        if (name == "pause")
            m_videoPage->setPaused(m_mpv.property("pause").toBool());
        if (name == "volume" || name == "mute")
            m_videoPage->setVolume(int(m_mpv.property("volume").toDouble()), m_mpv.property("mute").toBool());
        if (name == "video-params/aspect")
            m_videoPage->setAspect(m_mpv.property("video-params/aspect").toDouble());
        // The video page's own controls, also over the picture in the now
        // playing view.
        if (m_pages->currentWidget() == m_videoPage || panelVideo() || m_videoPage->miniShown()) {
            if (name == "time-pos" || name == "duration")
                m_videoPage->setTimes(m_mpv.property("time-pos").isValid() ? m_mpv.property("time-pos").toDouble() : -1,
                                      m_mpv.property("duration").isValid() ? m_mpv.property("duration").toDouble() : -1);
            if (name == "track-list")
                m_videoPage->setTracks(m_mpv.property("track-list").toList());
            if (name == "speed")
                m_videoPage->setSpeed(m_mpv.property("speed").toDouble());
            if (name == "ytdl-format") {
                static const QRegularExpression cap(QStringLiteral(R"(height<=(\d+))"));
                m_videoPage->setQualityCap(cap.match(m_mpv.property("ytdl-format").toString()).captured(1).toInt());
            }
        }
        // A reload at a second has started playing: later files start at 0.
        if (name == "time-pos" && m_resetStart && m_mpv.property("time-pos").toDouble() > 0) {
            m_resetStart = false;
            m_mpv.setProperty("start", "none");
        }
        // ...or once another entry comes up: the one reloaded at a second
        // never played (that second past its end), and the next must not
        // start there too.
        if (name == "playlist-pos" && m_resetStart && m_mpv.property("playlist-pos").toInt() != m_resetStartPos) {
            m_resetStart = false;
            m_mpv.setProperty("start", "none");
        }
        // mpv knows the length of what it plays; keep it for the list.
        if (name == "duration") {
            const QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString().isEmpty()
                ? m_mpv.property("path").toString() : m_mpv.property("user-data/ghost/source").toString());
            m_resolver.rememberDuration(id, int(m_mpv.property("duration").toDouble()));
            fillDurations();
        }
    });

    for (QAbstractScrollArea *area : {static_cast<QAbstractScrollArea *>(m_songs),
                                      static_cast<QAbstractScrollArea *>(m_libraryList),
                                      static_cast<QAbstractScrollArea *>(m_homeScroll)})
        new SmoothScroll(area);
    new SmoothScroll(m_videoPage->scrollArea());
    for (QWidget *area : m_searchPage->scrollAreas())
        new SmoothScroll(qobject_cast<QAbstractScrollArea *>(area));
    new SmoothScroll(qobject_cast<QAbstractScrollArea *>(m_panel->scrollArea()));
    m_songs->viewport()->installEventFilter(this);

    // Lists added, changed or deleted on disk show up without Refresh.
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_rescanSoon.start(); });
    m_rescanSoon.setSingleShot(true);
    m_rescanSoon.setInterval(600);
    connect(&m_rescanSoon, &QTimer::timeout, this, [this] {
        // Our own move just changed the folder, and the user may already be
        // dragging the next list; filling the panel again would drop it.
        if (QApplication::mouseButtons() != Qt::NoButton) {
            m_rescanSoon.start();
            return;
        }
        // Scanned first: a list that has just arrived in Downloads is only
        // known to the library after it.
        rescan();
        if (QSettings().value("library/autoImport").toBool())
            importStray(false);
    });

    qApp->installEventFilter(this);
    icons::setSkipSeconds(QSettings().value("playback/seekStep", 10).toInt());
    for (auto [keys, step] : {std::pair{QKeySequence::ZoomIn, 0.1}, std::pair{QKeySequence::ZoomOut, -0.1}}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, this, [this, step] { changeZoom(step); });
    }
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this), &QShortcut::activated, this, [this] { changeZoom(0); });
    const qreal savedZoom = QSettings().value("window/zoom", 1.0).toReal();
    if (!qFuzzyCompare(savedZoom, 1.0))
        QTimer::singleShot(0, this, [this, savedZoom] { changeZoom(savedZoom - theme::zoom()); });

    applyLook();
    buildTray();
    m_updates = new UpdateChecker(this);
    connect(m_updates, &UpdateChecker::finished, this, [this](bool ok) {
        const int behind = ok ? m_updates->newerCount() : 0;
        m_updateButton->setVisible(behind > 0);
        m_updateButton->setToolTip(behind == 1 ? QStringLiteral("An update is available")
                                               : QStringLiteral("%1 updates are available").arg(behind));
        if (!m_updateAsked)
            return;
        m_updateAsked = false;
        if (!ok)
            QMessageBox::information(this, "Updates", "Couldn't check for updates. Check your connection, "
                                                      "or run update.bat in the mpv folder.");
        else
            showUpdates();
    });
    // Once a day at most, a little after opening, in the background.
    QTimer::singleShot(8000, this, [this] {
        const QDateTime last = QSettings().value("updates/checkedAt").toDateTime();
        if (!last.isValid() || last.secsTo(QDateTime::currentDateTime()) > 20 * 3600)
            checkForUpdates(false);
    });
    connect(&m_mpv, &MpvClient::fileFailed, this, &MainWindow::songFailed);
    connect(&m_mpv, &MpvClient::fileLoaded, this, [this] {
        // Started with no sound device (a monitor's speakers asleep, a
        // headset off), mpv plays into nothing rather than ending every song
        // at once (--audio-fallback-to-null), and keeps that for the songs
        // after. Each new song asks for the real device again (2026-11-11).
        if (m_mpv.property("current-ao").toString() == QLatin1String("null"))
            m_mpv.command({"ao-reload"});
        m_failedSongs.clear();
        if (m_failNoticeShown && m_toast)
            m_toast->hide();
        m_failNoticeShown = false;
    });
    connect(&m_mpv, &MpvClient::connectionChanged, this, [this](bool on) {
        schedulePlace();
        if (!on) {
            m_mpvEmbedded = false;
            m_failedSongs.clear();   // the next mpv numbers its entries anew
            // Nothing plays: no tab is what plays.
            if (m_tabStrip && m_tabStrip->current >= 0 && !m_restartPlay) {
                m_tabStrip->current = -1;
                tabsChanged();
            }
            if (m_restartPlay) {
                const QueuedPlay q = *m_restartPlay;
                m_restartPlay.reset();
                QTimer::singleShot(200, this, [this, q] { play(q.path, q.shuffle, q.startAt, q.order); });
            }
            return;
        }
        playLog(QStringLiteral("mpv answered (pid %1)").arg(m_launchPid));
        m_launchPid = 0;
        // A new mpv numbers its entries from 1 again: what the queue knew
        // of the last one's would match the wrong songs.
        m_queueIds.clear();
        m_knownIds.clear();
        m_queuePending.clear();
        m_autoplayTitles.clear();
        m_autoplayFor.clear();
        m_mpvEmbedded = m_launching.isValid() && m_launchEmbedded;
        m_keepAspect = -1;
        sendWindowChoice();
        m_lastKindVideo = false;
        // Listening only goes on in a new mpv too.
        if (m_videoPage->listenOnly())
            m_mpv.setProperty("user-data/ghost/listen-only", true);
        m_launching.invalidate();
        // A Play pressed while mpv was starting goes to it now.
        if (m_queuedPlay) {
            const QueuedPlay q = *m_queuedPlay;
            m_queuedPlay.reset();
            play(q.path, q.shuffle, q.startAt, q.order);
        }
        // Once mpv has said which process it is, any other mpv this app
        // started (two Plays before the first one answered, in an older
        // build) is closed.
        QTimer::singleShot(1500, this, [this] { closeExtraPlayers(false); });
    });
    for (const QString &folder : QSettings().value("library/openFolders").toStringList())
        m_openFolders.insert(folder);
    if (QSettings().value("library/dense").toBool())
        setLibraryDense(true);
    rescan();
    showHome();
    QTimer::singleShot(800, this, [this] { importStray(true); });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Kept running in the tray (Settings off by default): the window goes,
    // the music does not.
    if (!m_quitting && m_tray && QSettings().value("tray/keepRunning", false).toBool()) {
        hide();
        event->ignore();
        return;
    }
    saveResume();   // before mpv goes
    if (m_mpvEmbedded && m_mpv.connected()) {
        m_mpv.command({"quit"});
        m_mpv.flush();
    }
    if (!QSettings().value("player/window", false).toBool() && m_mpv.connected() &&
        m_mpv.property("user-data/ghost/audio").toBool()) {
        m_mpv.command({"quit"});
        m_mpv.flush();
        closeExtraPlayers(true);
    }
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/split", m_split->saveState());
    settings.setValue("window/panelSplit", m_rightSplit->saveState());
    QMainWindow::closeEvent(event);
    if (m_tray)
        m_tray->hide();
    // The tray keeps Qt from quitting on the last window by itself.
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void MainWindow::quitForReal()
{
    m_quitting = true;
    close();
}

// -- window chrome -----------------------------------------------------------

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
        // The whole window is client area. Maximised, Windows still puts the
        // invisible resize border outside the screen; take it back in.
        if (IsZoomed(msg->hwnd) && !m_videoFull) {
            auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
            const int frame = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
            params->rgrc[0].left += frame;
            params->rgrc[0].top += frame;
            params->rgrc[0].right -= frame;
            params->rgrc[0].bottom -= frame;
        }
        *result = 0;
        return true;
    }
    if (msg->message == WM_NCHITTEST) {
        // Qt's own DPI handling, as in Elite: Windows' raw pixels drift on a
        // scaled laptop screen.
        const QPoint pos = mapFromGlobal(QCursor::pos());
        if (!isMaximized()) {
            const int w = width(), h = height(), m = 6;
            const bool l = pos.x() < m, r = pos.x() > w - m, t = pos.y() < m, b = pos.y() > h - m;
            if (l && t) { *result = HTTOPLEFT; return true; }
            if (r && t) { *result = HTTOPRIGHT; return true; }
            if (l && b) { *result = HTBOTTOMLEFT; return true; }
            if (r && b) { *result = HTBOTTOMRIGHT; return true; }
            if (l) { *result = HTLEFT; return true; }
            if (r) { *result = HTRIGHT; return true; }
            if (t) { *result = HTTOP; return true; }
            if (b) { *result = HTBOTTOM; return true; }
        }
        if (pos.y() < kTopBar) {
            // Buttons and the search box in the strip stay clickable; the rest
            // of it is the title bar Windows drags, snaps and double-clicks.
            QWidget *child = childAt(pos);
            for (QWidget *w = child; w; w = w->parentWidget())
                if (m_chromeButtons.contains(w))
                    return QMainWindow::nativeEvent(eventType, message, result);
            *result = HTCAPTION;
            return true;
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange && m_maxButton)
        m_maxButton->setText(QString(isMaximized() ? kIconRestore : kIconMax));
    if (event->type() == QEvent::WindowStateChange) {
        schedulePlace();   // the windows over the picture go and come with it
        QTimer::singleShot(0, this, [this] {
            keepNormalPlaceOnScreen();
            keepFrameStyle();
        });
    }
    // Brought up from the taskbar, Windows gives the keys back to the window
    // that last had them, which can be mpv's inside the picture: Space and
    // the other keys then went to mpv instead of here.
    if (event->type() == QEvent::ActivationChange && isActiveWindow())
        QTimer::singleShot(0, this, [this] {
            const HWND focus = GetFocus();
            DWORD pid = 0;
            if (focus)
                GetWindowThreadProcessId(focus, &pid);
            if (!focus || pid != GetCurrentProcessId())
                SetFocus(reinterpret_cast<HWND>(winId()));
        });
    QMainWindow::changeEvent(event);
}

QWidget *MainWindow::buildTopBar()
{
    m_backButton = glyphButton(QChar(0xE76B), "Go back (Alt+Left)", "pill");
    m_forwardButton = glyphButton(QChar(0xE76C), "Go forward (Alt+Right)", "pill");
    connect(m_backButton, &QPushButton::clicked, this, [this] { goHistory(false); });
    connect(m_forwardButton, &QPushButton::clicked, this, [this] { goHistory(true); });
    m_backButton->setEnabled(false);
    m_forwardButton->setEnabled(false);
    auto *home = glyphButton(kIconHome, "Home", "pill");
    connect(home, &QPushButton::clicked, this, &MainWindow::showHome);
    // YouTube: its own home and search, every video and not only music.
    auto *youtube = glyphButton(QChar(0xE714), "YouTube layout (click again for music)", "pill");
    m_youtubeButton = youtube;
    connect(youtube, &QPushButton::clicked, this, [this] { setYouTubeLayout(!m_youtubeLayout, true); });
    m_profileButton = new QPushButton;
    m_profileButton->setObjectName("profileButton");
    m_profileButton->setToolTip("Profile");
    m_profileButton->setCursor(Qt::PointingHandCursor);
    m_profileButton->setFixedSize(36, 36);
    m_profileButton->setIconSize(QSize(28, 28));
    connect(m_profileButton, &QPushButton::clicked, this, &MainWindow::showProfile);
    // Right-clicked: the profile's menu, with the private session.
    m_profileButton->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_profileButton, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.addAction("Profile", this, &MainWindow::showProfile);
        QAction *priv = menu.addAction("Private session", this, [this](bool on) { setPrivateSession(on); });
        priv->setCheckable(true);
        priv->setChecked(m_private);
        priv->setToolTip("Nothing goes to your history, top songs or Discord until you turn it off or close the app");
        menu.exec(m_profileButton->mapToGlobal(pos));
    });

    m_search = new QLineEdit;
    m_search->setObjectName("topSearch");
    m_search->setPlaceholderText("What do you want to play?");
    // A magnifier at the start, as Spotify's box has: without it the box
    // read as an empty strip and the search went unnoticed.
    {
        auto *glass = m_search->addAction(QIcon(), QLineEdit::LeadingPosition);
        const auto paintGlass = [glass] {
            QPixmap pixmap(32, 32);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::TextAntialiasing);
            QFont font(QStringLiteral("Segoe Fluent Icons"));
            font.setPixelSize(20);
            painter.setFont(font);
            painter.setPen(theme::c().dim);
            painter.drawText(pixmap.rect(), Qt::AlignCenter, QString(QChar(0xE721)));
            glass->setIcon(QIcon(pixmap));
        };
        paintGlass();
        theme::whenChanged(m_search, paintGlass);
    }
    m_search->setToolTip("Songs, videos and playlists on YouTube Music, with your own lists that match on top. "
                         "A YouTube playlist link opens that playlist.");
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(360);
    // YouTube Music is asked once typing pauses, or at once with Enter.
    m_searchTimer.setSingleShot(true);
    m_searchTimer.setInterval(450);
    connect(&m_searchTimer, &QTimer::timeout, this, &MainWindow::runSearch);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (text.trimmed().isEmpty())
            runSearch();
        else
            m_searchTimer.start();
    });
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        m_searchTimer.stop();
        runSearch();
    });

    auto *minimize = glyphButton(kIconMin, "Minimize", "chrome");
    m_maxButton = glyphButton(kIconMax, "Maximize", "chrome");
    auto *close = glyphButton(kIconClose, "Close", "chromeClose");
    auto *gear = glyphButton(QChar(0xE713), "Settings", "pill");
    m_gearButton = gear;
    connect(gear, &QPushButton::clicked, this, &MainWindow::showSettings);
    // Only there while something is behind, in the accent colour: noticed,
    // never in the way, and nothing opens by itself.
    m_updateButton = glyphButton(QChar(0xE896), "Updates are available", "pill");
    theme::style(m_updateButton, QStringLiteral("color: @accentText;"));
    m_updateButton->hide();
    connect(m_updateButton, &QPushButton::clicked, this, &MainWindow::showUpdates);
    connect(minimize, &QPushButton::clicked, this, &MainWindow::showMinimized);
    // Through Windows itself rather than Qt's showMaximized(): Qt resizes a
    // frameless window to the screen, so Windows took the full size for the
    // normal one, and dragging the window off maximised kept it full size.
    connect(m_maxButton, &QPushButton::clicked, this, [this] {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    });
    connect(close, &QPushButton::clicked, this, &MainWindow::close);
    m_chromeButtons = {m_backButton, m_forwardButton, m_profileButton, home, youtube, m_search, m_updateButton, gear, minimize, m_maxButton, close};

    m_topBar = new QWidget;
    m_topBar->setObjectName("topBar");
    m_topBar->setAttribute(Qt::WA_StyledBackground);
    m_topBar->setFixedHeight(kTopBar);
    auto *row = new QHBoxLayout(m_topBar);
    row->setContentsMargins(8, 0, 0, 0);
    row->setSpacing(8);
    row->addWidget(m_backButton);
    row->addWidget(m_forwardButton);
    row->addWidget(home);
    row->addWidget(youtube);
    m_searchButton = glyphButton(QChar(0xE721), "Search", "pill");
    connect(m_searchButton, &QPushButton::clicked, this, &MainWindow::openSearch);
    m_chromeButtons << m_searchButton;   // clicked, not a place to drag the window by
    row->addWidget(m_searchButton);
    row->addWidget(m_search);
    // The tabs after the search, the island centred in what is left.
    m_tabStrip = new TabStrip;
    m_tabStrip->hide();
    m_tabStrip->cover = [this](const QString &id) { return m_covers.get(id, ThumbnailCache::Size::Card); };
    m_tabStrip->onPick = [this](int i) { pickTab(i); };
    m_tabStrip->onClose = [this](int i) { closeTab(i); };
    m_tabStrip->onMenu = [this](int i, const QPoint &at) { tabMenu(i, at); };
    m_tabStrip->onMore = [this](const QPoint &at) { showAllTabs(at); };
    m_chromeButtons << m_tabStrip;   // clicked, not a place to drag the window by
    connect(&m_covers, &ThumbnailCache::ready, m_tabStrip, [this](const QString &id) {
        m_tabStrip->update();
        if (m_videoPage && m_videoPage->coverShown() && id == m_coverId)
            m_videoPage->setCover(true, m_covers.get(id, ThumbnailCache::Size::Hero));
    });
    // A moment after mpv's properties change, once they agree with each
    // other (the list's kind can come after the entry's path).
    m_tabsSoon.setSingleShot(true);
    m_tabsSoon.setInterval(400);
    connect(&m_tabsSoon, &QTimer::timeout, this, &MainWindow::syncTabs);
    {
        // Kept from the last time, none of them loaded (Settings > Remember
        // last tabs, on unless turned off).
        const QJsonArray saved = QSettings().value("youtube/rememberTabs", true).toBool()
            ? QJsonDocument::fromJson(QSettings().value("youtube/tabs").toByteArray()).array() : QJsonArray();
        for (const QJsonValue &v : saved) {
            const QJsonObject o = v.toObject();
            VideoTab t{o.value("target").toString(), o.value("id").toString(), o.value("title").toString(),
                       o.value("channel").toString(), o.value("list").toString(), o.value("seconds").toDouble()};
            if (!t.target.isEmpty())
                m_tabStrip->tabs << t;
        }
        m_tabStrip->changed();
        // Shown once the layout is known (they were loaded and never shown,
        // Batin, 2026-11-20).
        QTimer::singleShot(0, this, [this] { tabsChanged(); });
    }
    row->addWidget(m_tabStrip, 3);
    row->addStretch(2);
    row->addWidget(m_updateButton);
    row->addWidget(m_profileButton);
    row->addWidget(gear);
    row->addSpacing(10);
    row->addWidget(minimize);
    row->addWidget(m_maxButton);
    row->addWidget(close);
    return m_topBar;
}

// -- library panel -----------------------------------------------------------

QWidget *MainWindow::buildLibrary()
{
    m_libraryTitle = new QLabel("Your Library");
    m_libraryTitle->setObjectName("panelTitle");
    // Cut short with "..." rather than running under the buttons beside it.
    m_libraryTitle->setMinimumWidth(0);
    m_libraryTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_collapse = glyphButton(kIconCollapse, "Collapse library");
    connect(m_collapse, &QPushButton::clicked, this, [this] {
        const bool compact = !m_compact;
        const int left = compact ? compactWidth() : qRound(300 * theme::zoom());
        const QList<int> before = m_split->sizes();
        // Nothing is drawn until it is settled: in a small window the library
        // opened as far as the page let it (a strip of covers) for a frame
        // and folded back, a flick (Batın, 2026-11-11).
        setUpdatesEnabled(false);
        // Room made first: in a small window the page and the now playing
        // view kept the library at its covers-only width, and it was drawn
        // as the full panel squeezed into that.
        m_libraryWanted = left;
        updatePanelVisibility();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        setUpdatesEnabled(true);
        openLeftPanel(left, !compact, [this, compact, before](int got) {
            setUpdatesEnabled(false);
            m_libraryWanted = -1;
            if (!compact && got < compactBelow()) {
                // Still no room: back as it was, with a word why.
                m_split->setSizes(before);
                updatePanelVisibility();
                toast("Make the window wider to open the library");
            } else {
                setLibraryCompact(got < compactBelow());
            }
            setUpdatesEnabled(true);
        });
    });
    auto *add = glyphButton(kIconAdd, "New folder, add a folder, refresh");
    m_addButton = add;
    connect(add, &QPushButton::clicked, this, [this, add] { showLibraryMenu(add->mapToGlobal(QPoint(0, add->height()))); });
    // Covers or one line per list, as Spotify's "Compact" view.
    m_viewToggle = glyphButton(kIconDense, "Compact list");
    connect(m_viewToggle, &QPushButton::clicked, this, [this] { setLibraryDense(!m_rowDelegate->dense()); });

    // The library's own search, apart from the top box (Batın, 2026-11-06),
    // as Spotify's "Search in Your Library": a magnifier that opens a box
    // under the title; it filters only this panel.
    m_libraryFind = glyphButton(QChar(0xE721), "Search in Your Library");
    m_librarySearch = new QLineEdit;
    m_librarySearch->setObjectName("songSearch");
    m_librarySearch->setPlaceholderText("Search in Your Library");
    m_librarySearch->setClearButtonEnabled(true);
    m_librarySearch->hide();
    m_librarySearch->installEventFilter(this);
    connect(m_librarySearch, &QLineEdit::textChanged, this, &MainWindow::refill);
    // Opens it; it closes with Esc or when left empty (a toggle here met
    // the box closing itself as the click took the focus away).
    connect(m_libraryFind, &QPushButton::clicked, this, [this] {
        m_librarySearch->show();
        m_librarySearch->setFocus();
    });

    m_libraryHead = new QWidget;
    auto *head = new QHBoxLayout(m_libraryHead);
    head->setContentsMargins(0, 0, 0, 0);
    head->addWidget(m_collapse);
    head->addWidget(m_libraryTitle, 1);
    head->addWidget(m_libraryFind);
    head->addWidget(m_viewToggle);
    head->addWidget(add);

    m_chips = new QButtonGroup(this);
    m_chipRow = new QWidget;
    auto *chips = new QHBoxLayout(m_chipRow);
    chips->setContentsMargins(0, 0, 0, 0);
    chips->setSpacing(6);
    int id = 0;
    for (const char *label : {"All", "Songs", "Videos"}) {
        auto *chip = new QPushButton(label);
        chip->setObjectName("chip");
        chip->setCheckable(true);
        chip->setChecked(id == 0);
        chip->setCursor(Qt::PointingHandCursor);
        m_chips->addButton(chip, id++);
        chips->addWidget(chip);
    }
    chips->addStretch();
    connect(m_chips, &QButtonGroup::idClicked, this, [this](int which) {
        m_filter = static_cast<Filter>(which);
        refill();
    });

    auto *library = new LibraryList;
    library->onDrop = [this](const QStringList &moving, const QString &target, int position) {
        libraryDrop(moving, target, position);
    };
    library->setDragDropMode(QAbstractItemView::InternalMove);
    library->setDefaultDropAction(Qt::MoveAction);
    library->setDropIndicatorShown(true);
    m_libraryList = library;
    m_libraryList->setObjectName("library");
    m_libraryList->setMouseTracking(true);
    m_libraryList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // Names are cut with "..." to the panel's width: nothing to scroll
    // sideways, and the two bars' meeting corner showed as a black square.
    m_libraryList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_libraryList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_libraryEmpty = new QLabel;
    m_libraryEmpty->setObjectName("statLabel");
    m_libraryEmpty->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_libraryEmpty->setWordWrap(true);
    m_libraryEmpty->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_libraryEmpty->hide();
    auto *emptyLayout = new QVBoxLayout(m_libraryList->viewport());
    emptyLayout->setContentsMargins(12, 24, 12, 0);
    emptyLayout->addWidget(m_libraryEmpty);
    emptyLayout->addStretch();
    m_rowDelegate = new tiles::RowDelegate([this](const QModelIndex &index) {
        return coversFor(index.data(tiles::kIndexRole).toInt());
    }, m_libraryList);
    m_libraryList->setItemDelegate(m_rowDelegate);
    connect(m_libraryList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString folder = item->data(tiles::kFolderRole).toString();
        if (folder.isEmpty()) {
            openPlaylist(item->data(tiles::kIndexRole).toInt());
            // Found by a song in it: open on that song, the rest filtered out.
            const QString match = item->data(tiles::kMatchRole).toString();
            if (!match.isEmpty())
                m_songSearch->setText(match);
            return;
        }
        // A folder opens and closes in place. A double-click arrives as a
        // second click too; it must not close what the first one opened.
        if (folder == m_lastToggled && m_toggleClock.isValid() &&
            m_toggleClock.elapsed() < QApplication::doubleClickInterval())
            return;
        m_lastToggled = folder;
        m_toggleClock.start();
        if (m_openFolders.contains(folder))
            m_openFolders.remove(folder);
        else
            m_openFolders.insert(folder);
        saveOpenFolders();
        refill();
    });
    connect(m_libraryList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item->data(tiles::kFolderRole).toString().isEmpty())
            return;
        const auto &all = m_library.playlists();
        const int i = item->data(tiles::kIndexRole).toInt();
        playLog(QStringLiteral("double-click on library row %1").arg(i));
        if (i >= 0 && i < all.size())
            play(all[i].path);
    });
    connect(m_libraryList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_libraryList->itemAt(pos);
        if (!item)
            return;
        const QString folder = item->data(tiles::kFolderRole).toString();
        if (!folder.isEmpty()) {
            QMenu menu(this);
            const QString folderKey = item->data(tiles::kKeyRole).toString();
            menu.addAction(item->data(tiles::kPinnedRole).toBool() ? "Unpin folder" : "Pin folder", this,
                           [this, folderKey] { togglePin(folderKey); });
            menu.addAction("New playlist here...", this, [this, folder] { newPlaylist(folder); });
            menu.addAction("Rename folder", this, [this, folder] { renameFolder(folder); });
            menu.addAction("Show in folder", this, [folder] {
                QProcess::startDetached("explorer.exe", {QDir::toNativeSeparators(Library::homeFolder() + '/' + folder)});
            });
            menu.addSeparator();
            menu.addAction("Delete folder (keeps its playlists)", this, [this, folder] { deleteFolder(folder); });
            menu.exec(m_libraryList->viewport()->mapToGlobal(pos));
            return;
        }
        showPlaylistMenu(item->data(tiles::kIndexRole).toInt(), m_libraryList->viewport()->mapToGlobal(pos));
    });

    auto *panel = new QWidget;
    panel->setObjectName("libraryPanel");
    panel->setAttribute(Qt::WA_StyledBackground);
    panel->setMinimumWidth(compactWidth());
    // Compact or not follows the panel's real width, judged whenever it
    // changes: read in the constructor it was not laid out yet, looked
    // narrow, and the panel opened compact at full width.
    panel->installEventFilter(this);
    m_libraryPanel = panel;
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(10, 14, 6, 10);
    box->setSpacing(10);
    box->addWidget(m_libraryHead);
    box->addWidget(m_librarySearch);
    box->addWidget(m_chipRow);
    box->addWidget(m_libraryList, 1);
    return panel;
}

void MainWindow::showPlaylistMenu(int index, const QPoint &at)
{
    const auto &all = m_library.playlists();
    if (index < 0 || index >= all.size())
        return;
    const Playlist list = all[index];
    const QString path = list.path;
    QMenu menu(this);
    menu.addAction("Play", this, [this, path] { play(path); });
    menu.addAction("Shuffle", this, [this, path] { play(path, true); });
    const QString listKey = QFileInfo(path).fileName();
    menu.addSeparator();
    menu.addAction(QSettings().value("library/pinned").toStringList().contains(listKey) ? "Unpin playlist" : "Pin playlist", this,
                   [this, listKey] { togglePin(listKey); });
    // Only lists in the library proper go into its folders; one in
    // Downloads or an added folder is imported first.
    const QString home = Library::homeFolder();
    const QString dir = QDir::cleanPath(QFileInfo(path).absolutePath());
    if (dir.compare(home, Qt::CaseInsensitive) == 0 || dir.startsWith(home + '/', Qt::CaseInsensitive)) {
        QMenu *into = menu.addMenu("Move to folder");
        for (const QString &f : m_library.subfolders())
            if (f != list.folder)
                into->addAction(f, this, [this, path, f] { moveToFolder(path, f); });
        if (!into->isEmpty())
            into->addSeparator();
        into->addAction("New folder...", this, [this, path] { newFolder(path); });
        if (!list.folder.isEmpty())
            menu.addAction("Take out of \"" + list.folder + "\"", this, [this, path] { moveToFolder(path, QString()); });
    }
    menu.addAction("Rename", this, [this, index] { renamePlaylist(index); });
    menu.addAction("Change cover...", this, [this, index] { chooseCover(index); });
    const QString colorKey = "headerColor/" + QFileInfo(path).fileName();
    menu.addAction("Header colour...", this, [this, index, colorKey] {
        const QColor now = QSettings().value(colorKey).value<QColor>();
        const QColor picked = QColorDialog::getColor(now.isValid() ? now : theme::accent(), this, "Header colour");
        if (!picked.isValid())
            return;
        QSettings().setValue(colorKey, picked);
        if (index == m_open)
            static_cast<PlaylistHeader *>(m_playlistCover)->setTint(picked);
    });
    if (QSettings().contains(colorKey))
        menu.addAction("Header colour from the cover", this, [this, index, colorKey] {
            QSettings().remove(colorKey);
            if (index == m_open)
                static_cast<PlaylistHeader *>(m_playlistCover)->setTint(QColor());
        });
    if (QFileInfo::exists(customCoverPath(path)))
        menu.addAction("Remove custom image", this, [this, index] { removeCover(index); });
    menu.addAction(m_offline.isMarked(path) ? "Remove download" : "Download", this, [this, index] { toggleDownload(index); });
    menu.addAction(all[index].description.isEmpty() ? "Add a description..." : "Edit description...", this,
                   [this, index] { editDescription(index); });
    menu.addAction("Share", this, [this, index] { shareList(index); });
    menu.addAction("Export a copy...", this, [this, index] { exportPlaylist(index); });
    menu.addAction("Show in folder", this, [path] {
        QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
    });
    menu.addSeparator();
    menu.addAction("Delete", this, [this, index] { deletePlaylist(index); });
    menu.exec(at);
}

void MainWindow::setLibraryCompact(bool compact)
{
    m_compact = compact;
    m_libraryTitle->setVisible(!compact);
    m_chipRow->setVisible(!compact);
    m_addButton->setVisible(!compact);
    m_viewToggle->setVisible(!compact);
    m_libraryFind->setVisible(!compact);
    if (compact) {
        m_librarySearch->clear();
        m_librarySearch->hide();
    }
    m_collapse->setText(QString(compact ? kIconExpand : kIconCollapse));
    m_collapse->setToolTip(compact ? "Expand library" : "Collapse library");
    m_rowDelegate->setCompact(compact);
    // The video page's cinema is its own button's now (Batin, 2026-11-21),
    // not the library's folding.
    // A scroll bar in a cover-wide panel would cut the covers off.
    m_libraryList->setVerticalScrollBarPolicy(compact ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    m_libraryList->doItemsLayout();
    // Rows are wider than the panel while it is dragged from covers-only to
    // full and back, and the list had scrolled sideways with them: the covers
    // stood cut off at the panel's left edge.
    m_libraryList->horizontalScrollBar()->setValue(0);
}

void MainWindow::showLibraryMenu(const QPoint &at)
{
    QMenu menu(this);
    menu.addAction("New playlist...", this, [this] { newPlaylist(); });
    menu.addAction("New folder...", this, [this] { newFolder(); });
    menu.addAction("Import from Spotify...", this, &MainWindow::importSpotify);
    menu.addSeparator();
    menu.addAction("Add a folder with playlists...", this, &MainWindow::addFolder);
    menu.addAction("Refresh", this, &MainWindow::rescan);
    menu.addSeparator();
    menu.addAction("Open the library folder", this, [] {
        QProcess::startDetached("explorer.exe", {QDir::toNativeSeparators(Library::homeFolder())});
    });
    menu.exec(at);
}

void MainWindow::setLibraryDense(bool dense)
{
    m_rowDelegate->setDense(dense);
    QSettings().setValue("library/dense", dense);
    m_viewToggle->setText(QString(dense ? kIconCovers : kIconDense));
    m_viewToggle->setToolTip(dense ? "List with covers" : "Compact list");
    refill();
    m_libraryList->doItemsLayout();
}

QStringList MainWindow::arranged(const QStringList &keys, const QString &level) const
{
    QSettings settings;
    const QStringList pinned = settings.value("library/pinned").toStringList();
    const QStringList placed = settings.value("library/order/" + (level.isEmpty() ? QStringLiteral("_top") : level)).toStringList();
    QStringList out, fresh, rest;
    for (const QString &k : pinned)
        if (keys.contains(k))
            out << k;
    for (const QString &k : keys)
        if (!pinned.contains(k) && !placed.contains(k))
            fresh << k;
    for (const QString &k : placed)
        if (keys.contains(k) && !pinned.contains(k))
            rest << k;
    return out + fresh + rest;
}

void MainWindow::libraryDrop(const QStringList &moving, const QString &targetKey, int position)
{
    if (moving.isEmpty())
        return;
    const auto &all = m_library.playlists();
    auto folderOf = [&](const QString &key) -> QString {
        if (key.startsWith(QStringLiteral("folder:")))
            return QString();
        for (const Playlist &p : all)
            if (QFileInfo(p.path).fileName() == key)
                return p.folder;
        return QString();
    };
    // Where the rows land: which level (the library, or a folder) and before
    // which row of it. QAbstractItemView: 0 on the item, 1 above, 2 below,
    // 3 the empty space under the rows.
    QString dest, before;
    bool atEnd = false, atStart = false;
    if (targetKey.isEmpty() || position == 3) {
        atEnd = true;
    } else if (targetKey.startsWith(QStringLiteral("folder:"))) {
        const QString folder = targetKey.mid(7);
        if (position == 0) {
            dest = folder;
            atEnd = true;
        } else if (position == 1) {
            before = targetKey;
        } else if (m_openFolders.contains(folder)) {
            dest = folder;   // just under an open folder's row: first in it
            atStart = true;
        } else {
            // After a closed folder, at the top level: before what follows it.
            QStringList top;
            for (int r = 0; r < m_libraryList->count(); ++r) {
                const auto *item = m_libraryList->item(r);
                if (!item->data(tiles::kInFolderRole).toBool())
                    top << item->data(tiles::kKeyRole).toString();
            }
            const int at = top.indexOf(targetKey);
            before = at + 1 < top.size() ? top[at + 1] : QString();
            atEnd = before.isEmpty();
        }
    } else {
        dest = folderOf(targetKey);
        before = targetKey;
        if (position == 2) {
            // After a list: before the next row of the same level.
            QStringList level;
            for (int r = 0; r < m_libraryList->count(); ++r) {
                const auto *item = m_libraryList->item(r);
                const QString k = item->data(tiles::kKeyRole).toString();
                if (item->data(tiles::kInFolderRole).toBool() == !dest.isEmpty() &&
                    (dest.isEmpty() || folderOf(k) == dest))
                    level << k;
            }
            const int at = level.indexOf(targetKey);
            before = at + 1 < level.size() ? level[at + 1] : QString();
            atEnd = before.isEmpty();
        }
    }
    if (moving.contains(before))
        return;   // dropped onto itself

    // Folders stay at the top level; lists change folder on disk first.
    QStringList placed;
    QString reopen;
    const QString openPath = m_open >= 0 ? all[m_open].path : QString();
    for (const QString &key : moving) {
        if (key.startsWith(QStringLiteral("folder:"))) {
            if (dest.isEmpty())
                placed << key;
            continue;
        }
        QString newKey = key;
        if (folderOf(key) != dest) {
            QString path;
            for (const Playlist &p : all)
                if (QFileInfo(p.path).fileName() == key)
                    path = p.path;
            const QString moved = m_library.moveToSubfolder(path, dest);
            if (moved.isEmpty())
                continue;
            movedPlayed(path, moved);
            if (path == openPath)
                reopen = moved;
            newKey = QFileInfo(moved).fileName();
            if (newKey != key)
                renameKey(key, newKey);
        }
        placed << newKey;
    }
    if (placed.isEmpty()) {
        rescan();
        return;
    }

    // The level's order as it is shown now, with the moved rows put in.
    QStringList current;
    for (int r = 0; r < m_libraryList->count(); ++r) {
        const auto *item = m_libraryList->item(r);
        const QString k = item->data(tiles::kKeyRole).toString();
        const bool inFolder = item->data(tiles::kInFolderRole).toBool();
        if (dest.isEmpty() ? !inFolder : (inFolder && folderOf(k) == dest))
            current << k;
    }
    for (const QString &k : moving)
        current.removeAll(k);
    for (const QString &k : placed)
        current.removeAll(k);
    const int at = atStart ? 0 : atEnd || before.isEmpty() ? int(current.size()) : qMax(0, int(current.indexOf(before)));
    for (int i = 0; i < placed.size(); ++i)
        current.insert(at + i, placed[i]);
    QSettings().setValue("library/order/" + (dest.isEmpty() ? QStringLiteral("_top") : dest), current);
    if (!dest.isEmpty()) {
        m_openFolders.insert(dest);
        saveOpenFolders();
    }
    if (!reopen.isEmpty())
        reopenAfter(reopen);
    else
        rescan();
}

void MainWindow::togglePin(const QString &key)
{
    QSettings settings;
    QStringList pinned = settings.value("library/pinned").toStringList();
    if (pinned.contains(key))
        pinned.removeAll(key);
    else
        pinned << key;
    settings.setValue("library/pinned", pinned);
    refill();
}

void MainWindow::renameKey(const QString &from, const QString &to)
{
    // A folder renamed, or a list moved under a new file name: its pin and
    // place go with it.
    QSettings settings;
    QStringList pinned = settings.value("library/pinned").toStringList();
    pinned.replaceInStrings(QRegularExpression("^" + QRegularExpression::escape(from) + "$"), to);
    settings.setValue("library/pinned", pinned);
    settings.beginGroup("library/order");
    for (const QString &level : settings.childKeys()) {
        QStringList order = settings.value(level).toStringList();
        order.replaceInStrings(QRegularExpression("^" + QRegularExpression::escape(from) + "$"), to);
        settings.setValue(level, order);
    }
    if (from.startsWith(QStringLiteral("folder:")) && settings.contains(from.mid(7))) {
        settings.setValue(to.mid(7), settings.value(from.mid(7)));
        settings.remove(from.mid(7));
    }
    settings.endGroup();
}

void MainWindow::saveOpenFolders()
{
    QSettings().setValue("library/openFolders", QStringList(m_openFolders.begin(), m_openFolders.end()));
}

void MainWindow::newFolder(const QString &thenMove)
{
    bool ok = false;
    const QString typed = QInputDialog::getText(this, "New folder", "Folder name:", QLineEdit::Normal, QString(), &ok);
    const QString name = Library::cleanFolderName(typed);
    if (!ok || name.isEmpty())
        return;
    if (!m_library.subfolders().contains(name, Qt::CaseInsensitive) && !m_library.createSubfolder(name)) {
        QMessageBox::warning(this, "New folder", "The folder couldn't be made.");
        return;
    }
    m_openFolders.insert(name);
    saveOpenFolders();
    if (!thenMove.isEmpty())
        moveToFolder(thenMove, name);
    else
        rescan();
}

void MainWindow::moveToFolder(const QString &path, const QString &folder)
{
    const bool wasOpen = m_open >= 0 && m_library.playlists()[m_open].path == path;
    const QString moved = m_library.moveToSubfolder(path, folder);
    if (moved.isEmpty()) {
        QMessageBox::warning(this, "Move to folder", "The playlist couldn't be moved.");
        return;
    }
    movedPlayed(path, moved);
    if (wasOpen)
        reopenAfter(moved);
    else
        rescan();
}

void MainWindow::renameFolder(const QString &folder)
{
    bool ok = false;
    const QString typed = QInputDialog::getText(this, "Rename folder", "Folder name:", QLineEdit::Normal, folder, &ok);
    const QString name = Library::cleanFolderName(typed);
    if (!ok || name.isEmpty() || name == folder)
        return;
    const QString openPath = m_open >= 0 ? m_library.playlists()[m_open].path : QString();
    if (!m_library.renameSubfolder(folder, name)) {
        QMessageBox::warning(this, "Rename folder", "The folder couldn't be renamed. Is a list in it playing?");
        return;
    }
    const QString from = Library::homeFolder() + '/' + folder + '/';
    const QString to = Library::homeFolder() + '/' + name + '/';
    for (const Playlist &p : m_library.playlists())
        if (p.folder == folder)
            movedPlayed(p.path, to + QFileInfo(p.path).fileName());
    if (m_openFolders.remove(folder))
        m_openFolders.insert(name);
    saveOpenFolders();
    renameKey(QStringLiteral("folder:") + folder, QStringLiteral("folder:") + name);
    if (openPath.startsWith(from, Qt::CaseInsensitive))
        reopenAfter(to + openPath.mid(from.size()));
    else
        rescan();
}

void MainWindow::deleteFolder(const QString &folder)
{
    if (QMessageBox::question(this, "Delete folder",
            QStringLiteral("Delete the folder \"%1\"?\n\nIts playlists stay in your library.").arg(folder)) != QMessageBox::Yes)
        return;
    const QString openPath = m_open >= 0 ? m_library.playlists()[m_open].path : QString();
    QString reopen;
    for (const Playlist &p : m_library.playlists()) {
        if (p.folder != folder)
            continue;
        const QString to = Library::homeFolder() + '/' + QFileInfo(p.path).fileName();
        movedPlayed(p.path, to);
        if (p.path == openPath)
            reopen = to;
    }
    if (!m_library.dissolveSubfolder(folder))
        QMessageBox::warning(this, "Delete folder",
            "Its playlists were moved out, but the folder has other files in it, so it was left in place.");
    m_openFolders.remove(folder);
    saveOpenFolders();
    if (!reopen.isEmpty())
        reopenAfter(reopen);
    else
        rescan();
}

// -- pages ---------------------------------------------------------------------

QWidget *MainWindow::buildHome()
{
    m_greeting = new QLabel;
    m_greeting->setObjectName("pageTitle");

    auto *stats = new QHBoxLayout;
    stats->setSpacing(12);
    stats->addWidget(statCard(m_statLists, "playlists"));
    stats->addWidget(statCard(m_statSongs, "songs and videos"));
    stats->addWidget(statCard(m_statHours, "hours of music, roughly"));
    stats->addWidget(statCard(m_statNew, "added this week"));

    m_recentAdded = makeShelf();
    m_recentPlayed = makeShelf();
    m_recentPlayedTitle = sectionTitle("Jump back in");

    // A first start shows zeros and empty shelves, and nothing says where a
    // list comes from: the bot on Discord, whose .m3u lands in Downloads,
    // which the library already watches.
    m_welcome = new QWidget;
    m_welcome->setObjectName("welcome");
    m_welcome->setAttribute(Qt::WA_StyledBackground);
    auto *welcomeTitle = new QLabel("Your library is empty");
    welcomeTitle->setObjectName("welcomeTitle");
    auto *welcomeText = new QLabel(
        "Playlists come from the Ghost Engine bot on Discord. Add it to a server, find songs with "
        "<b>!s</b>, queue them with <b>!add</b>, then <b>!export</b>.<br>"
        "The .m3u you download shows up here by itself.");
    welcomeText->setObjectName("welcomeText");
    welcomeText->setWordWrap(true);
    auto *addBot = new QPushButton("Add the bot to Discord");
    addBot->setObjectName("welcomeAction");
    addBot->setCursor(Qt::PointingHandCursor);
    connect(addBot, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QString::fromLatin1(kBotInvite)));
    });
    auto *openFile = new QPushButton("Open a .m3u");
    openFile->setObjectName("videoAction");
    openFile->setCursor(Qt::PointingHandCursor);
    connect(openFile, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, "Open a playlist",
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), "Playlists (*.m3u *.m3u8)");
        if (!files.isEmpty())
            importFiles(files);
    });
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addWidget(addBot);
    buttons->addWidget(openFile);
    buttons->addStretch();
    auto *welcomeBox = new QVBoxLayout(m_welcome);
    welcomeBox->setContentsMargins(22, 18, 22, 18);
    welcomeBox->setSpacing(8);
    welcomeBox->addWidget(welcomeTitle);
    welcomeBox->addWidget(welcomeText);
    welcomeBox->addSpacing(4);
    welcomeBox->addLayout(buttons);
    m_welcome->hide();

    auto *page = new QWidget;
    page->setObjectName("home");
    auto *box = new QVBoxLayout(page);
    box->setContentsMargins(28, 22, 28, 16);
    box->setSpacing(12);
    box->addWidget(m_greeting);
    box->addSpacing(4);
    box->addWidget(m_welcome);
    box->addLayout(stats);
    box->addSpacing(10);
    box->addWidget(sectionTitle("Recently added"));
    box->addWidget(m_recentAdded);
    box->addWidget(m_recentPlayedTitle);
    box->addWidget(m_recentPlayed);
    box->addStretch();

    auto *scroll = new QScrollArea;
    m_homeScroll = scroll;
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    return scroll;
}

QListWidget *MainWindow::makeShelf()
{
    auto *shelf = new QListWidget;
    shelf->setObjectName("grid");
    shelf->setViewMode(QListView::IconMode);
    shelf->setMovement(QListView::Static);
    shelf->setWrapping(false);
    shelf->setFlow(QListView::LeftToRight);
    shelf->setUniformItemSizes(true);
    shelf->setMouseTracking(true);
    shelf->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    shelf->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    shelf->setFixedHeight(kTile + 70 + 26);
    shelf->setItemDelegate(new tiles::TileDelegate(kTile, [this](const QModelIndex &index) {
        return coversFor(index.data(tiles::kIndexRole).toInt());
    }, shelf));
    connect(shelf, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        openPlaylist(item->data(tiles::kIndexRole).toInt());
    });
    connect(shelf, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const auto &all = m_library.playlists();
        const int i = item->data(tiles::kIndexRole).toInt();
        if (i >= 0 && i < all.size())
            play(all[i].path);
    });
    return shelf;
}

QWidget *MainWindow::buildPlaylistPage()
{
    auto *header = new PlaylistHeader([this] { return coversFor(m_open); });
    m_playlistCover = header;
    header->setFixedHeight(headerTall());
    m_playlistKind = new QLabel;
    m_playlistKind->setObjectName("kicker");
    m_playlistName = new QLabel;
    m_playlistName->setObjectName("bigTitle");
    m_playlistName->setToolTip("Double-click to rename");
    m_nameEdit = new QLineEdit(header);
    m_nameEdit->setObjectName("nameEdit");
    m_nameEdit->hide();
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &MainWindow::finishRename);
    m_nameEdit->installEventFilter(this);
    m_playlistName->installEventFilter(this);
    header->installEventFilter(this);
    header->setMouseTracking(true);
    header->setToolTip("Click the cover to choose your own picture");
    m_playlistName->setMinimumWidth(10);
    m_playlistName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_playlistInfo = new QLabel;
    m_playlistInfo->setObjectName("heroInfo");
    auto *text = new QVBoxLayout;
    text->setSpacing(4);
    text->addStretch();
    text->addWidget(m_playlistKind);
    text->addWidget(m_playlistName);
    m_playlistDesc = new QLabel;
    m_playlistDesc->setObjectName("heroInfo");
    m_playlistDesc->setCursor(Qt::PointingHandCursor);
    m_playlistDesc->setToolTip("Click to edit the description");
    m_playlistDesc->setMinimumWidth(10);
    m_playlistDesc->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_playlistDesc->installEventFilter(this);
    text->addWidget(m_playlistDesc);
    text->addWidget(m_playlistInfo);
    m_sharedBy = new QWidget;
    m_sharedAvatar = new QLabel;
    m_sharedAvatar->setFixedSize(22, 22);
    m_sharedText = new QLabel;
    m_sharedText->setObjectName("heroInfo");
    auto *sharedRow = new QHBoxLayout(m_sharedBy);
    sharedRow->setContentsMargins(0, 2, 0, 0);
    sharedRow->setSpacing(8);
    sharedRow->addWidget(m_sharedAvatar);
    sharedRow->addWidget(m_sharedText, 1);
    m_sharedBy->hide();
    text->addWidget(m_sharedBy);
    connect(&m_pictures, &RemotePictures::ready, this, [this](const QString &url) {
        if (url == m_sharedAvatarUrl && m_open >= 0)
            showSharedBy(m_library.playlists()[m_open].shared);
    });
    m_headRow = new QHBoxLayout(header);
    m_headRow->addLayout(text, 1);

    auto *playButton = new icons::IconButton(icons::Icon::Play, 56, "Play");
    playButton->setRound(theme::accent(), theme::accentText(), theme::c().onAccent);
    theme::whenChanged(playButton, [playButton] {
        playButton->setRound(theme::accent(), theme::accentText(), theme::c().onAccent);
    });
    auto *shuffleButton = new icons::IconButton(icons::Icon::Shuffle, 40, "Shuffle");
    auto *folderButton = new icons::IconButton(icons::Icon::Folder, 40, "Show in folder");
    connect(playButton, &QAbstractButton::clicked, this, [this] { playOpen(0); });
    shuffleButton->setToolTip("Shuffle the order of this list");
    m_pageShuffle = shuffleButton;
    theme::whenChanged(shuffleButton, [this, shuffleButton] {
        const bool on = m_sortKey == SortKey::Shuffled;
        shuffleButton->setColors(on ? theme::accentText() : theme::c().dim, on ? theme::accentText().lighter(120) : theme::c().strong);
    });
    connect(shuffleButton, &QAbstractButton::clicked, this, [this] {
        // On: the songs in a new random order. Off: back to the list's own.
        // Nothing starts playing; Play then follows the order shown.
        if (m_open < 0)
            return;
        if (m_sortKey == SortKey::Shuffled) {
            sortBy(SortKey::Custom);
        } else {
            m_shuffleSeed = QRandomGenerator::global()->generate();
            sortBy(SortKey::Shuffled);
        }
    });
    connect(folderButton, &QAbstractButton::clicked, this, [this] {
        if (m_open >= 0)
            QProcess::startDetached("explorer.exe",
                {"/select,", QDir::toNativeSeparators(m_library.playlists()[m_open].path)});
    });
    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(28, 4, 28, 4);
    actions->setSpacing(18);
    actions->addWidget(playButton);
    actions->addWidget(shuffleButton);
    actions->addWidget(folderButton);
    m_downloadButton = new QPushButton(QString(QChar(0xE896)));
    m_downloadButton->setObjectName("bigGlyph");
    m_downloadButton->setCursor(Qt::PointingHandCursor);
    connect(m_downloadButton, &QPushButton::clicked, this, [this] { toggleDownload(m_open); });
    actions->addWidget(m_downloadButton);
    auto *shareButton = new QPushButton(QString(QChar(0xE72D)));
    shareButton->setObjectName("bigGlyph");
    shareButton->setCursor(Qt::PointingHandCursor);
    shareButton->setToolTip("Share: copies the list to paste into a Discord chat");
    connect(shareButton, &QPushButton::clicked, this, [this] { shareList(m_open); });
    actions->addWidget(shareButton);
    actions->addStretch();
    m_songSearch = new QLineEdit;
    m_songSearch->setObjectName("songSearch");
    m_songSearch->setPlaceholderText("Search in playlist");
    m_songSearch->setClearButtonEnabled(true);
    m_songSearch->setFixedWidth(240);
    connect(m_songSearch, &QLineEdit::textChanged, this, &MainWindow::filterSongs);
    actions->addWidget(m_songSearch);

    // "#  Title · Artist" over the songs, with a hairline under it. Each name
    // sorts by itself, as in Spotify: once up, once down, a third time back
    // to the list's own order ("#" goes straight back).
    auto *columns = new QWidget;
    m_columnsBar = columns;
    columns->setObjectName("columns");
    columns->setAttribute(Qt::WA_StyledBackground);
    auto *colRow = new QHBoxLayout(columns);
    colRow->setContentsMargins(0, 6, 0, 8);
    colRow->setSpacing(0);
    auto sortButton = [this](const QString &text, SortKey key, const QString &tip) {
        auto *b = new QPushButton(text);
        b->setObjectName("columnButton");
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(tip);
        b->setProperty("label", text);
        connect(b, &QPushButton::clicked, this, [this, key] { sortBy(key); });
        m_sortButtons.append({key, b});
        return b;
    };
    auto *hash = sortButton("#", SortKey::Custom, "The list's own order");
    hash->setFixedWidth(44);
    auto *titleButton = sortButton("Title", SortKey::Title, "Sort by title");
    auto *dot = new QLabel(QStringLiteral("·"));
    dot->setObjectName("columnLabel");
    auto *artistButton = sortButton("Artist", SortKey::Artist, "Sort by artist");
    colRow->addWidget(hash);
    colRow->addSpacing(4);
    colRow->addWidget(titleButton);
    colRow->addSpacing(6);
    colRow->addWidget(dot);
    colRow->addSpacing(6);
    colRow->addWidget(artistButton);
    colRow->addStretch(1);
    m_addedHeader = sortButton("Date added", SortKey::Added, "Sort by when songs were added to the list");
    m_addedHeader->setFixedWidth(tiles::kAddedColumn);
    colRow->addWidget(m_addedHeader);
    auto *clock = sortButton(QString(), SortKey::Duration, "Sort by length");
    clock->setFixedSize(tiles::kDurationColumn, 18);
    clock->setObjectName("clockHeader");
    clock->installEventFilter(this);
    colRow->addWidget(clock);
    colRow->addSpacing(8 + 12);   // the rows' right margin, and the scroll bar beside them

    m_songs = new QListWidget;
    m_songs->setObjectName("songs");
    m_songs->setMouseTracking(true);
    m_songs->setUniformItemSizes(true);
    m_songs->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_rowDelegateSongs = new tiles::SongDelegate([this](const QModelIndex &index) {
        return m_covers.get(idForTarget(index.data(Qt::UserRole).toString()), ThumbnailCache::Size::Card);
    }, m_songs);
    m_songs->setItemDelegate(m_rowDelegateSongs);
    m_cardDelegate = new tiles::VideoCardDelegate([this](const QModelIndex &index) {
        return m_covers.get(idForTarget(index.data(Qt::UserRole).toString()), ThumbnailCache::Size::Hero);
    }, m_songs);
    m_cardDelegate->channelFor = [this](const QModelIndex &index) {
        return videoByline(idForTarget(index.data(Qt::UserRole).toString()));
    };
    // Scrolling the songs folds the header into a slim bar, as Spotify does:
    // the cover and title shrink over the first 170 px of scrolling.
    // Not while the bar's handle is held: the header's height is taken from
    // the list's, so the range under the mouse changed as it moved and the
    // handle jumped rows. It folds or opens once let go.
    connect(m_songs->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::foldHeader);
    connect(m_songs->verticalScrollBar(), &QScrollBar::sliderReleased, this, &MainWindow::foldHeader);
    connect(m_songs, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        playLog(QStringLiteral("double-click on row %1 of list %2").arg(m_songs->row(item)).arg(m_open));
        playOpen(m_songs->row(item));
    });
    // Songs are dragged to a new place in the list (in its own order only)
    // and removed with Delete or the menu; Ctrl+Z puts the file back.
    m_songs->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_songs->setDefaultDropAction(Qt::MoveAction);
    m_songs->setDragDropMode(QAbstractItemView::InternalMove);
    connect(m_songs->model(), &QAbstractItemModel::rowsMoved, this, [this] {
        // After the drop has finished, not in the middle of it.
        QTimer::singleShot(0, this, &MainWindow::saveSongOrder);
    });
    m_songs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_songs, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_songs->itemAt(pos);
        if (!item)
            return;
        if (!m_videoGrid &&
            tiles::SongDelegate::likeRect(m_songs->visualItemRect(item)).adjusted(-4, -4, 4, 4).contains(QPointF(pos))) {
            const QString artist = item->data(tiles::kArtistRole).toString();
            const QString track = item->text();
            likeMenu(item->data(Qt::UserRole).toString(), artist.isEmpty() ? track : artist + QStringLiteral(" - ") + track,
                     m_songs->viewport()->mapToGlobal(pos));
            markLikedRows();
            m_songs->viewport()->update();
            return;
        }
        if (!item->isSelected()) {
            m_songs->clearSelection();
            item->setSelected(true);
        }
        const int count = m_songs->selectedItems().size();
        QMenu menu(this);
        menu.addAction("Play", this, [this, item] { playOpen(m_songs->row(item)); });
        if (count == 1) {
            const QString target = item->data(Qt::UserRole).toString();
            const QString title = item->text();
            const QString artist = item->data(tiles::kArtistRole).toString();
            menu.addAction("About this song", this, [this, target, title, artist] { showSongInfo(target, title, artist); });
            if (!artist.isEmpty())
                menu.addAction("Go to artist", this, [this, artist] { goToArtist(artist); });
            // As Spotify's: the song's radio, and a link to send (2026-11-11).
            const QString id = idForTarget(target);
            const bool audio = listIsAudio(m_library.playlists().value(m_open).path);
            if (!id.isEmpty() && audio)
                menu.addAction("Go to song radio", this, [this, id, title, artist] {
                    ytmusic::Item song;
                    song.kind = ytmusic::Kind::Song;
                    song.id = id;
                    song.title = title;
                    song.artist = artist;
                    m_searchPage->playRadio(song);
                });
            if (!id.isEmpty())
                menu.addAction(audio ? "Copy song link" : "Copy video link", this, [this, id, audio] { copyLink(id, audio); });
        }
        if (listIsAudio(m_library.playlists().value(m_open).path)) {
            // In the order the page shows them, not the order they were picked.
            QVector<PlaylistEntry> picked;
            for (int r = 0; r < m_songs->count(); ++r) {
                QListWidgetItem *row = m_songs->item(r);
                if (!row->isSelected())
                    continue;
                const QString artist = row->data(tiles::kArtistRole).toString();
                picked << PlaylistEntry{artist.isEmpty() ? row->text() : artist + QStringLiteral(" - ") + row->text(),
                                        row->data(Qt::UserRole).toString(), {}};
            }
            menu.addAction("Add to queue", this, [this, picked] { queueSongs(picked, false); });
            menu.addAction("Play next", this, [this, picked] { queueSongs(picked, true); });
        }
        QMenu *to = menu.addMenu(count > 1 ? QStringLiteral("Add %1 songs to playlist").arg(count) : QStringLiteral("Add to playlist"));
        to->addAction("New playlist...", this, [this] { newPlaylist(QString(), true, m_library.playlists().value(m_open).audio); });
        to->addSeparator();
        const QString here = m_library.playlists().value(m_open).path;
        const bool hereAudio = m_library.playlists().value(m_open).audio;
        const QString home = Library::homeFolder();
        for (const Playlist &p : m_library.playlists()) {
            const QString dir = QDir::cleanPath(QFileInfo(p.path).absolutePath());
            const bool ours = dir.compare(home, Qt::CaseInsensitive) == 0 || dir.startsWith(home + '/', Qt::CaseInsensitive);
            if (p.path == here || !ours || p.audio != hereAudio)
                continue;   // outside the library: the bot's to write; the other kind: the other side's
            const QString path = p.path;
            to->addAction(p.folder.isEmpty() ? p.name : p.folder + QStringLiteral("  ›  ") + p.name,
                          this, [this, path] { addSelectedSongsTo(path); });
        }
        menu.addSeparator();
        menu.addAction(count > 1 ? QStringLiteral("Remove %1 songs from this playlist").arg(count)
                                 : QStringLiteral("Remove from this playlist"),
                       this, &MainWindow::removeSelectedSongs);
        if (m_undoPath == m_library.playlists().value(m_open).path)
            menu.addAction("Undo last change\tCtrl+Z", this, &MainWindow::undoSongEdit);
        menu.exec(m_songs->viewport()->mapToGlobal(pos));
    });
    auto *remove = new QShortcut(QKeySequence::Delete, m_songs);
    remove->setContext(Qt::WidgetShortcut);
    connect(remove, &QShortcut::activated, this, &MainWindow::removeSelectedSongs);
    auto *undo = new QShortcut(QKeySequence::Undo, m_songs);
    undo->setContext(Qt::WidgetWithChildrenShortcut);
    connect(undo, &QShortcut::activated, this, &MainWindow::undoSongEdit);

    m_emptyHint = new QLabel("Nothing here yet.\nRight-click a song in any playlist and choose Add to playlist.");
    m_emptyHint->setObjectName("statLabel");
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->hide();

    auto *list = new QVBoxLayout;
    list->setContentsMargins(20, 0, 16, 0);
    list->setSpacing(4);
    list->addWidget(columns);
    list->addWidget(m_emptyHint);
    list->addWidget(m_songs, 1);
    buildRecommended();   // a card of its own under the page (see the middle column)

    auto *page = new QWidget;
    auto *box = new QVBoxLayout(page);
    box->setContentsMargins(0, 0, 0, 6);
    box->setSpacing(8);
    box->addWidget(header);
    box->addLayout(actions);
    box->addLayout(list, 1);
    return page;
}

int MainWindow::listPageOverhead() const
{
    // What a list's page needs under its header to stay readable: the
    // buttons, the column names and two songs, with the gaps between.
    if (!m_listPage || !m_listPage->layout())
        return 0;
    QLayout *page = m_listPage->layout();
    const QMargins m = page->contentsMargins();
    int need = m.top() + m.bottom() + 2 * qMax(0, page->spacing());
    if (page->count() > 1)
        need += page->itemAt(1)->sizeHint().height();   // the buttons
    if (page->count() > 2 && page->itemAt(2)->layout() && page->itemAt(2)->layout()->count() > 0)
        need += page->itemAt(2)->layout()->itemAt(0)->sizeHint().height() + 4;   // the column names
    return need + 2 * int(56 * theme::zoom());
}

int MainWindow::headerTallNow() const
{
    // Never taller than the page leaves room for (Batin, 2026-11-12: with
    // Recommended open in a small window, the buttons and the column names
    // lay over the big cover): down to the folded header when it must.
    const int page = m_pages ? m_pages->height() : 0;
    return qBound(listheader::folded(), page - listPageOverhead(), listheader::tallFor(page));
}

void MainWindow::foldHeader()
{
    QScrollBar *bar = m_songs->verticalScrollBar();
    if (bar->isSliderDown())
        return;
    m_playlistCover->setFixedHeight(listheader::heightFor(listheader::foldFor(bar->value()), headerTallNow()));
    layoutHeader();
}

void MainWindow::layoutHeader()
{
    if (!m_playlistCover || !m_headRow)
        return;   // a resize while the window is still being built
    auto *header = static_cast<PlaylistHeader *>(m_playlistCover);
    listheader::arrange(header, m_headRow, m_playlistKind, m_playlistInfo);
    showDescription();
    if (m_open >= 0)
        m_sharedBy->setVisible(header->height() > 150 * theme::zoom() && !m_library.playlists()[m_open].shared.isEmpty());
    fitTitle();
    header->update();
}

void MainWindow::fitTitle()
{
    // The largest size the name fits at, from the header's size down to a
    // floor; a name too long even then is cut with an ellipsis.
    if (m_open < 0)
        return;
    const QString name = m_library.playlists()[m_open].name;
    const auto *header = static_cast<PlaylistHeader *>(m_playlistCover);
    const int available = header->width() - header->coverRect().right() - 60;
    QString shown;
    const qreal size = listheader::titlePoints(m_playlistName->font(), name, available,
                                               listheader::foldOf(header, headerTallNow()), &shown);
    m_titlePt = size;
    const bool renaming = m_nameEdit && m_nameEdit->isVisible();
    m_playlistName->setStyleSheet(titleSheet(size, renaming));
    m_playlistName->setText(shown);
    if (renaming) {
        // Narrowed or widened with the panels, as the songs below are.
        m_nameEdit->setStyleSheet(QStringLiteral("font-size: %1pt;").arg(size));
        placeNameEdit();
    }
}

void MainWindow::placeNameEdit()
{
    // Over the title, wherever the layout has just put it, and never wider
    // than the header (the panels can narrow it while the box is open).
    QRect r = m_playlistName->geometry().adjusted(-8, -4, 8, 4);
    r.setRight(qMin(r.right(), m_playlistCover->width() - 12));
    m_nameEdit->setGeometry(r);
}

QString MainWindow::titleSheet(qreal points, bool hidden)
{
    // The size is the title's own; its tooltip ("Double-click to rename")
    // would otherwise take it too and show at 40 points.
    return theme::css(QStringLiteral("QLabel { font-size: %1pt; %2 } QToolTip { font-size: 9pt; font-weight: 400; color: @text; }"))
        .arg(points).arg(hidden ? QStringLiteral("color: transparent;") : QString());
}

void MainWindow::renamePlaylist(int index)
{
    // Right where the name is, as in Spotify: the title becomes a text box.
    if (index != m_open)
        openPlaylist(index);
    if (m_open < 0)
        return;
    m_nameEdit->setText(m_library.playlists()[m_open].name);
    m_nameEdit->setFont(m_playlistName->font());
    m_nameEdit->setStyleSheet(QStringLiteral("font-size: %1pt;").arg(m_titlePt));
    placeNameEdit();
    // The title stays in the layout, only invisible, so nothing around it moves.
    m_playlistName->setStyleSheet(titleSheet(m_titlePt, true));
    m_nameEdit->show();
    m_nameEdit->raise();
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void MainWindow::finishRename()
{
    if (!m_nameEdit->isVisible())
        return;
    m_nameEdit->hide();
    fitTitle();
    if (m_open >= 0 && !m_renameCancelled)
        renameTo(m_open, m_nameEdit->text().trimmed());
    m_renameCancelled = false;
}

void MainWindow::renameTo(int index, const QString &typed)
{
    const auto &all = m_library.playlists();
    if (index < 0 || index >= all.size())
        return;
    const Playlist p = all[index];
    if (typed.isEmpty() || typed == p.name)
        return;
    if (!Playlist::writeName(p.path, typed)) {
        QMessageBox::warning(this, "Rename playlist", "The playlist file couldn't be written.");
        return;
    }
    reopenAfter(p.path);

}

void MainWindow::movedPlayed(const QString &from, const QString &to)
{
    // Keep "Jump back in" pointing at a list that moved or was renamed.
    QSettings settings;
    QStringList played = settings.value("recent/played").toStringList();
    for (QString &path : played)
        if (path.compare(from, Qt::CaseInsensitive) == 0)
            path = to;
    settings.setValue("recent/played", played);
}

void MainWindow::importStray(bool ask)
{
    // The bot's exports waiting in Downloads: lists with ytdl:// entries.
    const QString downloads = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    QStringList stray;
    for (const Playlist &p : m_library.playlists()) {
        if (QDir::cleanPath(QFileInfo(p.path).absolutePath()).compare(downloads, Qt::CaseInsensitive) != 0)
            continue;
        if (!p.shared.isEmpty()) {   // a friend's, through Discord
            stray << p.path;
            continue;
        }
        for (const QString &target : p.preview)
            if (target.startsWith("ytdl://")) { stray << p.path; break; }
    }
    if (stray.isEmpty())
        return;

    QSettings settings;
    if (ask) {
        if (settings.contains("library/autoImport"))
            return;   // asked once already
        const auto answer = QMessageBox::question(this, "Ghost Engine",
            stray.size() == 1
                ? QStringLiteral("There is a playlist from the bot in your Downloads folder.\n\n"
                                 "Move it into your library (Music\\Ghost Engine)? New ones will then "
                                 "move in by themselves while Ghost Engine is open.")
                : QStringLiteral("There are %1 playlists from the bot in your Downloads folder.\n\n"
                                 "Move them into your library (Music\\Ghost Engine)? New ones will then "
                                 "move in by themselves while Ghost Engine is open.").arg(stray.size()));
        settings.setValue("library/autoImport", answer == QMessageBox::Yes);
        if (answer != QMessageBox::Yes)
            return;
    } else if (!settings.value("library/autoImport").toBool()) {
        return;
    }
    const QString openPath = m_open >= 0 ? m_library.playlists()[m_open].path : QString();
    QString reopen;
    for (const QString &path : stray) {
        const QString moved = m_library.importFile(path);
        if (moved.isEmpty())
            continue;
        movedPlayed(path, moved);
        if (path == openPath)
            reopen = moved;
    }
    if (!reopen.isEmpty())
        reopenAfter(reopen);
    else
        rescan();
}

void MainWindow::fillDurations()
{
    for (int row = 0; row < m_songs->count(); ++row) {
        QListWidgetItem *item = m_songs->item(row);
        const QString id = idForTarget(item->data(Qt::UserRole).toString());
        const int seconds = m_resolver.durationFor(id);
        if (seconds <= 0)
            m_resolver.requestDuration(id);
        item->setData(tiles::kDurationRole, seconds > 0
            ? QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0')) : QString());
    }
    markLikedRows();
}

QString MainWindow::videoByline(const QString &id)
{
    // "Channel  ·  3 days ago", as YouTube writes under a video; each part
    // is looked up in the background the first time it is asked for.
    QStringList parts;
    const QString channel = m_resolver.channelFor(id);
    if (channel.isEmpty())
        m_resolver.requestChannel(id);
    else
        parts << channel;
    const QDate uploaded = m_resolver.uploadDateFor(id);
    if (!uploaded.isValid()) {
        m_resolver.requestUploadDate(id);
    } else {
        const qint64 days = uploaded.daysTo(QDate::currentDate());
        parts << (days < 1 ? QStringLiteral("today")
                  : days < 2 ? QStringLiteral("1 day ago")
                  : days < 7 ? QStringLiteral("%1 days ago").arg(days)
                  : days < 14 ? QStringLiteral("1 week ago")
                  : days < 31 ? QStringLiteral("%1 weeks ago").arg(days / 7)
                  : days < 60 ? QStringLiteral("1 month ago")
                  : days < 365 ? QStringLiteral("%1 months ago").arg(days / 30)
                  : days < 730 ? QStringLiteral("1 year ago")
                  : QStringLiteral("%1 years ago").arg(days / 365));
    }
    return parts.join(QStringLiteral("  ·  "));
}

QString MainWindow::relativeTime(const QDateTime &when)
{
    const qint64 days = when.daysTo(QDateTime::currentDateTime());
    if (days < 1) return "Today";
    if (days < 2) return "Yesterday";
    if (days < 7) return QStringLiteral("%1 days ago").arg(days);
    if (days < 14) return "1 week ago";
    if (days < 31) return QStringLiteral("%1 weeks ago").arg(days / 7);
    if (days < 60) return "1 month ago";
    if (days < 365) return QStringLiteral("%1 months ago").arg(days / 30);
    return QLocale().toString(when.date(), QLocale::ShortFormat);
}

void MainWindow::deletePlaylist(int index)
{
    const auto &all = m_library.playlists();
    if (index < 0 || index >= all.size())
        return;
    const Playlist p = all[index];
    if (QMessageBox::question(this, "Delete playlist",
            QStringLiteral("Move \"%1\" to the Recycle Bin?").arg(p.name)) != QMessageBox::Yes)
        return;
    if (!QFile::moveToTrash(p.path)) {
        QMessageBox::warning(this, "Delete playlist", "Windows wouldn't move it to the Recycle Bin.");
        return;
    }
    if (index == m_open)
        showHome();
    rescan();
}

void MainWindow::reopenAfter(const QString &path)
{
    m_library.rescan();
    m_open = -1;
    refill();
    const auto &all = m_library.playlists();
    for (int i = 0; i < all.size(); ++i)
        if (all[i].path.compare(path, Qt::CaseInsensitive) == 0)
            openPlaylist(i);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // The page shorter or taller (Recommended pulled, the window resized):
    // the header's tall height follows; the card resized: so does the sheet.
    if (watched == m_pages && event->type() == QEvent::Resize && m_playlistCover)
        QTimer::singleShot(0, this, &MainWindow::foldHeader);
    if (watched == m_mainPanel && event->type() == QEvent::Resize && m_recCard)
        QTimer::singleShot(0, this, &MainWindow::fitRecSheet);
    if (watched == m_recCard && m_recShade &&
        (event->type() == QEvent::Move || event->type() == QEvent::Resize || event->type() == QEvent::Show ||
         event->type() == QEvent::Hide)) {
        auto *shade = static_cast<tiles::Shade *>(m_recShade);
        const bool shown = event->type() != QEvent::Hide && m_recCard->isVisibleTo(m_mainPanel);
        shade->setGeometry(0, m_recCard->y() - tiles::Shade::depth(), m_mainPanel->width(), tiles::Shade::depth());
        shade->setStrength(shown ? 1.0 : 0.0);
        if (shown)
            shade->raise();
    }
    // A click on a place that takes no focus (the page's empty space, a
    // header, a label) leaves the box being typed in, as in a browser; before,
    // only Esc or Enter did (Batin, 2026-11-12). The boxes that edit
    // something keep what was typed: the rename box saves on leaving, as on
    // Enter. Other windows (a popup, a dialog) are left alone.
    if (event->type() == QEvent::MouseButtonPress && watched->isWidgetType()) {
        auto *clicked = static_cast<QWidget *>(watched);
        QWidget *focus = QApplication::focusWidget();
        const bool typing = qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus) ||
                            qobject_cast<QPlainTextEdit *>(focus) || qobject_cast<QAbstractSpinBox *>(focus);
        if (typing && clicked != focus && !focus->isAncestorOf(clicked) &&
            clicked->window() == focus->window() && !(clicked->focusPolicy() & Qt::ClickFocus))
            focus->clearFocus();
    }
    if (m_ytChannels && watched == m_ytChannels->viewport() && event->type() == QEvent::Resize)
        padYtChannels();
    if (watched == m_ytSidebar && event->type() == QEvent::Resize) {
        const bool compact = m_ytSidebar->width() < qRound(150 * theme::zoom());
        if (compact != m_ytCompact)
            setYtCompact(compact);
    }
    // The library's search box closes with Esc, or when left empty.
    if (watched == m_librarySearch) {
        // Esc is the window's shortcut for leaving full screen; in the box
        // it is the box's.
        if (event->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            event->accept();
            return true;
        }
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            m_librarySearch->clear();
            m_librarySearch->hide();
            return true;
        }
        if (event->type() == QEvent::FocusOut && m_librarySearch->text().isEmpty())
            QTimer::singleShot(0, m_librarySearch, &QWidget::hide);
    }
    if (dragOrResize(watched, event))
        return true;
    if (event->type() == QEvent::MouseButtonPress && videoShowing() &&
        (watched == m_split->handle(1) || watched == m_rightSplit->handle(1)))
        m_pressedStill = grabStill();
    if (event->type() == QEvent::MouseButtonRelease && watched == m_split->handle(1))
        QTimer::singleShot(0, this, &MainWindow::snapCompactLibrary);
    // Space plays and pauses on every page. Before, only the video page had
    // it; elsewhere it pressed whatever button had the focus, or nothing
    // after the app was brought up from the taskbar.
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        QWidget *focus = QApplication::focusWidget();
        const bool typing = qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus) ||
                            qobject_cast<QPlainTextEdit *>(focus) || qobject_cast<QAbstractSpinBox *>(focus) ||
                            qobject_cast<QComboBox *>(focus);
        QWidget *active = QApplication::activeWindow();
        const bool ours = active && (active == this || active->parentWidget() == this ||
                                     active == m_videoPage->controls() || active == m_videoPage->corners());
        // Every shortcut is taken here, before the widget with the focus,
        // so a key works on every page; a text box keeps its letters.
        if (ours && !shortcuts::capturing() && !QApplication::activePopupWidget()) {
            const QString action = shortcuts::actionFor(shortcuts::keyOf(key));
            const shortcuts::Action *a = action.isEmpty() ? nullptr : shortcuts::find(action);
            if (a && (!typing || a->whileTyping)) {
                if (!key->isAutoRepeat() || a->repeats)
                    runShortcut(action);
                return true;
            }
        }
    }
    // The picture and the controls over it are windows that take no focus
    // (WindowDoesNotAcceptFocus), so a click on the video coming from
    // another app left that app in front: the keys pressed next (I, F,
    // Space) went to it, not here (2026-11-11). The app comes forward.
    if (event->type() == QEvent::MouseButtonPress && watched->isWidgetType()) {
        QWidget *top = static_cast<QWidget *>(watched)->window();
        if ((top == m_videoPage->corners() || top == m_videoPage->controls()) && !isActiveWindow()) {
            const HWND hwnd = reinterpret_cast<HWND>(winId());
            SetForegroundWindow(hwnd);
            activateWindow();
        }
    }
    // The mouse's side buttons, when one is given something to do.
    if (event->type() == QEvent::MouseButtonPress && !shortcuts::capturing()) {
        const QString action = shortcuts::actionFor(shortcuts::keyOfMouse(static_cast<QMouseEvent *>(event)->button()));
        QWidget *active = QApplication::activeWindow();
        if (!action.isEmpty() && active && (active == this || active->parentWidget() == this)) {
            runShortcut(action);
            return true;
        }
    }
    if (watched == m_pages && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        schedulePlace();
    if (watched == m_libraryPanel && event->type() == QEvent::Resize && m_rowDelegate && !m_videoFull) {
        fitLibraryHead();
        const bool compact = m_libraryPanel->width() < compactBelow();
        if (compact != m_compact)
            setLibraryCompact(compact);
        if (!(QApplication::mouseButtons() & Qt::LeftButton))
            QTimer::singleShot(0, this, &MainWindow::snapCompactLibrary);
        m_libraryList->horizontalScrollBar()->setValue(0);
        fitLibraryHead();
    }
    if (m_panel && watched == m_panel->coverArea() && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        schedulePlace();
    // The title moves after fitTitle has run, once the layout gets to it.
    if (watched == m_playlistName && (event->type() == QEvent::Move || event->type() == QEvent::Resize) &&
        m_nameEdit->isVisible())
        placeNameEdit();
    // Esc is also the window's (it leaves full screen) and a shortcut would
    // take it before the box: claim it, so the key below reaches us.
    if (watched == m_nameEdit && event->type() == QEvent::ShortcutOverride &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        event->accept();
        return true;
    }
    if (watched == m_nameEdit && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        m_renameCancelled = true;
        finishRename();
        return true;
    }
    // Ctrl + wheel zooms, as in a browser; only while the app is the window
    // in use. Windows sends the wheel to whatever is under the pointer, and
    // in a game Ctrl (crouch) and the wheel (weapons) went on and on over
    // Ghost Engine on the second screen: it shrank to 0.7 (Batın, 2026-11-08).
    if (event->type() == QEvent::Wheel && (static_cast<QWheelEvent *>(event)->modifiers() & Qt::ControlModifier) &&
        isActiveWindow()) {
        changeZoom(static_cast<QWheelEvent *>(event)->angleDelta().y() > 0 ? 0.1 : -0.1);
        return true;
    }
    // The play mark in a song's number column starts the list from there.
    // (Filters are installed while the page is still being built, so the
    // song list may not exist yet when the first events arrive.)
    // "Date added" shows over the rows exactly when they have room for it
    // (the same sum SongDelegate makes).
    if (m_songs && watched == m_songs->viewport() && event->type() == QEvent::Resize && m_addedHeader) {
        const qreal rowHeight = 56 * theme::zoom() - 2;
        const qreal coverRight = 44 + 4 + (rowHeight - 12) * 16 / 9;
        const qreal addedLeft = m_songs->viewport()->width() - 4 - 8 - tiles::kDurationColumn - tiles::kAddedColumn;
        m_addedHeader->setVisible(addedLeft - coverRight > 260);
    }
    if (m_songs && watched == m_songs->viewport() && event->type() == QEvent::Resize && m_videoGrid)
        fitVideoGrid();
    if (m_songs && watched == m_songs->viewport() && event->type() == QEvent::MouseButtonRelease && m_videoGrid) {
        // A card is played with one click, as on YouTube.
        auto *mouse = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item = m_songs->itemAt(mouse->position().toPoint());
        if (item && mouse->button() == Qt::LeftButton && !(mouse->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) &&
            m_open >= 0) {
            playOpen(m_songs->row(item));
            return true;
        }
    }
    // A press over the song rows that some other widget took (an overlay,
    // a page still on its way in) is logged by name.
    if (m_songs && m_songs->isVisible() && watched->isWidgetType() && watched != m_songs->viewport() &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)) {
        const QPoint global = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        const QRect rows(m_songs->viewport()->mapToGlobal(QPoint(0, 0)), m_songs->viewport()->size());
        if (rows.contains(global))
            playLog(QStringLiteral("%1 over the rows went to %2 \"%3\"")
                        .arg(event->type() == QEvent::MouseButtonDblClick ? "double-click" : "press")
                        .arg(QString::fromLatin1(watched->metaObject()->className()), watched->objectName()));
    }
    // A double-click on a song plays it, handled here rather than through
    // QListWidget's itemDoubleClicked: that one fires only when the second
    // press lands on the row the view kept from the first, and the first
    // double-click after a list page opened did nothing at all, not even
    // reaching the signal (ghost-play.log, 2026-11-11). Presses and
    // double-clicks are logged, for the next one that does nothing.
    if (m_songs && watched == m_songs->viewport() && !m_videoGrid &&
        (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPoint at = mouse->position().toPoint();
        QListWidgetItem *item = m_songs->itemAt(at);
        const bool dbl = event->type() == QEvent::MouseButtonDblClick;
        playLog(QStringLiteral("%1 at %2,%3 on row %4, list %5, scrolled %6, focus %7")
                    .arg(dbl ? "double-click" : "press").arg(at.x()).arg(at.y())
                    .arg(item ? m_songs->row(item) : -1).arg(m_open)
                    .arg(m_songs->verticalScrollBar()->value()).arg(m_songs->hasFocus()));
        if (dbl && item && mouse->button() == Qt::LeftButton && m_open >= 0) {
            // The number column and the + play or like on the press's
            // release already; a double-click there does not do it twice.
            const bool handled = mouse->position().x() < 48 ||
                tiles::SongDelegate::likeRect(m_songs->visualItemRect(item)).adjusted(-4, -4, 4, 4).contains(mouse->position());
            if (!handled)
                playOpen(m_songs->row(item));
            return true;
        }
    }
    if (m_songs && watched == m_songs->viewport() && event->type() == QEvent::MouseButtonRelease && !m_videoGrid) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item = m_songs->itemAt(mouse->position().toPoint());
        // The + left of the length: into Liked songs, or out of it.
        if (item && mouse->button() == Qt::LeftButton &&
            tiles::SongDelegate::likeRect(m_songs->visualItemRect(item)).adjusted(-4, -4, 4, 4).contains(mouse->position())) {
            const QString artist = item->data(tiles::kArtistRole).toString();
            const QString track = item->text();
            toggleLiked(item->data(Qt::UserRole).toString(), artist.isEmpty() ? track : artist + QStringLiteral(" - ") + track);
            return true;
        }
        if (item && mouse->button() == Qt::LeftButton && mouse->position().x() < 48 && m_open >= 0) {
            playOpen(m_songs->row(item));
            return true;
        }
    }
    if (watched == m_playlistDesc && event->type() == QEvent::MouseButtonRelease && m_open >= 0 &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        editDescription(m_open);
        return true;
    }
    if (m_recList && watched == m_recList->viewport() && event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        const QPoint at = static_cast<QMouseEvent *>(event)->position().toPoint();
        if (QListWidgetItem *item = m_recList->itemAt(at))
            if (tiles::SongDelegate::likeRect(m_recList->visualItemRect(item)).adjusted(-4, -4, 4, 4).contains(QPointF(at))) {
                addRecommended(m_recList->row(item));
                return true;
            }
    }
    if (watched->objectName() == "clockHeader" && event->type() == QEvent::Paint) {
        auto *w = static_cast<QPushButton *>(watched);
        QPainter p(w);
        const bool active = m_sortKey == SortKey::Duration;
        const QColor ink = active || w->underMouse() ? theme::c().text : theme::muted();
        // The lengths are right-aligned a little in from this edge; the
        // clock sits over the middle of a "3:16".
        icons::paint(p, icons::Icon::Clock, QRectF(w->width() - 25, 1, 16, 16), ink);
        if (active) {
            p.setPen(theme::accent());
            p.drawText(QRectF(0, 0, w->width() - 31, w->height()), Qt::AlignRight | Qt::AlignVCenter,
                       m_sortDescending ? QStringLiteral("▾") : QStringLiteral("▴"));
        }
        return true;
    }
    if (watched == m_playlistCover && m_open >= 0) {
        auto *header = static_cast<PlaylistHeader *>(m_playlistCover);
        if (event->type() == QEvent::ContextMenu) {
            auto *menuEvent = static_cast<QContextMenuEvent *>(event);
            if (header->coverRect().contains(menuEvent->pos())) {
                showPlaylistMenu(m_open, menuEvent->globalPos());
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove)
            header->setCursor(header->coverRect().contains(static_cast<QMouseEvent *>(event)->position().toPoint())
                              ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (event->type() == QEvent::MouseButtonRelease &&
            static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton &&
            header->coverRect().contains(static_cast<QMouseEvent *>(event)->position().toPoint())) {
            chooseCover(m_open);
            return true;
        }
    }
    if (watched == m_playlistName && event->type() == QEvent::MouseButtonDblClick) {
        // On the name itself, and not the second click of a panel edge just
        // dragged over it.
        const QPoint at = static_cast<QMouseEvent *>(event)->position().toPoint();
        const int textWidth = m_playlistName->fontMetrics().horizontalAdvance(m_playlistName->text());
        const bool onText = at.x() <= textWidth + 8;
        const bool dragging = m_splitMoved.isValid() && m_splitMoved.elapsed() < 800;
        if (onText && !dragging)
            renamePlaylist(m_open);
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

// The left panel (library or YouTube menu) set to `left` wide, then `done`
// with the width it got. Opening, a window too narrow for it grows by what
// it lacks, as far as the screen goes, rather than saying no; the window
// takes its new size a moment later, so the panel is opened again then. A
// window that still leaves no usable panel goes back to its size.
void MainWindow::openLeftPanel(int left, bool grow, const std::function<void(int)> &done)
{
    const auto open = [this, left] {
        const int total = m_split->sizes().value(0) + m_split->sizes().value(1);
        m_split->setSizes({left, total - left});
        return m_split->sizes().value(0);
    };
    const QList<int> before = m_split->sizes();
    const int got = open();
    if (!grow || got >= left || isMaximized() || isFullScreen() || !screen()) {
        done(got);
        return;
    }
    const QRect room = screen()->availableGeometry();
    const QRect frame = frameGeometry();
    const int more = qMin(left - got, room.width() - frame.width());
    if (more <= 0) {
        done(got);
        return;
    }
    // Not drawn half open meanwhile: as it was until the window is wider.
    m_split->setSizes(before);
    const int x = qBound(room.left(), frame.x(), room.right() + 1 - (frame.width() + more));
    const QRect was = geometry();
    setGeometry(was.translated(x - frame.x(), 0).adjusted(0, 0, more, 0));
    QTimer::singleShot(60, this, [this, open, before, was, done] {
        const int now = open();
        if (now >= qRound(150 * theme::zoom())) {
            done(now);
            return;
        }
        m_split->setSizes(before);
        setGeometry(was);
        done(before.value(0));
    });
}

void MainWindow::snapCompactLibrary()
{
    // Covers only is covers wide: with the chips' width as the line, a
    // panel left between the two (zoomed in, or let go there) stood wide
    // with its covers in the middle of it.
    if (!m_compact || m_videoFull || !m_split)
        return;
    const QList<int> sizes = m_split->sizes();
    if (sizes.value(0) <= compactWidth() + 4)
        return;
    const int total = sizes.value(0) + sizes.value(1);
    m_split->setSizes({compactWidth(), total - compactWidth()});
    syncSearchWidth();
}

void MainWindow::restoreLibraryWidth()
{
    if (m_videoFull || !m_restoreLibraryUntil.isValid() || m_restoreLibraryUntil.elapsed() > 1000)
        return;
    const QList<int> sizes = m_split->sizes();
    const int total = sizes.value(0) + sizes.value(1);
    m_split->setSizes({m_libraryWidthBeforeFull, total - m_libraryWidthBeforeFull});
    setLibraryCompact(m_compactBeforeFull);
    syncSearchWidth();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    fitTabs();
    restoreLibraryWidth();
    // Narrowed until the library is squeezed below its full width, it goes
    // covers-only and covers wide: only a dragged edge did that before, so
    // a small window kept a wide strip with the covers in its middle.
    QTimer::singleShot(0, this, [this] {
        if (!m_split || m_videoFull || isMinimized())
            return;
        if (!m_compact && m_split->sizes().value(0) < compactBelow())
            setLibraryCompact(true);
        snapCompactLibrary();
    });
    layoutHeader();
    syncSearchWidth();
    updatePanelVisibility();
    if (m_playlistCover)
        foldHeader();
    // The picture held still while the window changes size, as while a
    // panel's edge is dragged: mpv redrawn at every size stood a frame at
    // its old size over the list beside it, and on the way into or out of
    // full screen showed in its new place over the old picture, or black
    // (filmed). It comes back once the size has settled.
    if (videoShowing() && !isMinimized()) {
        startSurfaceDrag();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        placeSurface();
    }
    schedulePlace();
}

bool MainWindow::dragOrResize(QObject *watched, QEvent *event)
{
    // The window is dragged by its top strip and resized at its edges
    // through Qt as well as through WM_NCHITTEST: once a video has played,
    // Qt keeps a child window over the whole client area and Windows asks
    // that one where the mouse is, so the strip no longer read as a title
    // bar. Qt still hands the mouse to the widgets, and these calls go to
    // Windows' own move and size loops (snapping included).
    const QEvent::Type type = event->type();
    if (type == QEvent::Leave && watched == this && m_edgeCursor) {
        QGuiApplication::restoreOverrideCursor();
        m_edgeCursor = false;
        return false;
    }
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonDblClick && type != QEvent::MouseMove)
        return false;
    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || widget->window() != this || !windowHandle() || m_videoFull)
        return false;
    auto *mouse = static_cast<QMouseEvent *>(event);
    const QPoint at = mapFromGlobal(mouse->globalPosition().toPoint());
    Qt::Edges edges;
    if (!isMaximized()) {
        const int m = 6;
        if (at.x() < m) edges |= Qt::LeftEdge;
        if (at.x() >= width() - m) edges |= Qt::RightEdge;
        if (at.y() < m) edges |= Qt::TopEdge;
        if (at.y() >= height() - m) edges |= Qt::BottomEdge;
    }
    if (type == QEvent::MouseMove) {
        if (mouse->buttons() != Qt::NoButton)
            return false;
        if (edges) {
            const bool diagonal = (edges & (Qt::LeftEdge | Qt::RightEdge)) && (edges & (Qt::TopEdge | Qt::BottomEdge));
            const bool falling = edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge);
            const Qt::CursorShape shape = diagonal ? (falling ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor)
                                          : (edges & (Qt::LeftEdge | Qt::RightEdge)) ? Qt::SizeHorCursor : Qt::SizeVerCursor;
            if (m_edgeCursor)
                QGuiApplication::changeOverrideCursor(shape);
            else
                QGuiApplication::setOverrideCursor(shape);
            m_edgeCursor = true;
        } else if (m_edgeCursor) {
            QGuiApplication::restoreOverrideCursor();
            m_edgeCursor = false;
        }
        return false;
    }
    if (mouse->button() != Qt::LeftButton)
        return false;
    if (type == QEvent::MouseButtonPress && edges) {
        windowHandle()->startSystemResize(edges);
        return true;
    }
    if (at.y() >= kTopBar)
        return false;
    for (QWidget *w = widget; w; w = w->parentWidget())
        if (m_chromeButtons.contains(w))
            return false;   // buttons and the search box stay clickable
    if (type == QEvent::MouseButtonDblClick) {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    } else {
        windowHandle()->startSystemMove();
    }
    return true;
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    schedulePlace();   // the windows over the picture follow
    // Dropped on another screen's top edge, Windows maximises it there but
    // puts the normal place back where the drag began; Qt still calls the
    // window normal at this move, so Windows' own word is asked a bit later.
    QTimer::singleShot(0, this, [this] { keepNormalPlaceOnScreen(); });
}

void MainWindow::keepNormalPlaceOnScreen()
{
    // Maximised and taken to another screen (dragged by the strip, or
    // Win+Shift+arrow), the window's normal place stayed on the first one.
    // Minimised and brought back (Win+3, the taskbar), Windows maximises it
    // on the screen of that place: it jumped back to the first screen. The
    // normal place goes along, the same spot on the new screen's work area.
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!IsZoomed(hwnd) || m_videoFull)
        return;
    WINDOWPLACEMENT wp{sizeof(wp)};
    MONITORINFO here{sizeof(here)}, was{sizeof(was)};
    if (!GetWindowPlacement(hwnd, &wp) ||
        !GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &here) ||
        !GetMonitorInfo(MonitorFromRect(&wp.rcNormalPosition, MONITOR_DEFAULTTONEAREST), &was) ||
        EqualRect(&was.rcMonitor, &here.rcMonitor))
        return;
    // Placements are in workspace coordinates: each screen's corner is its
    // work area's (the taskbar left out), so a place is kept relative to
    // the screen's corner and fitted to its work area.
    const RECT &normal = wp.rcNormalPosition;
    const int workW = here.rcWork.right - here.rcWork.left, workH = here.rcWork.bottom - here.rcWork.top;
    const int w = std::min<int>(normal.right - normal.left, workW);
    const int h = std::min<int>(normal.bottom - normal.top, workH);
    const int x = here.rcMonitor.left + std::clamp<int>(normal.left - was.rcMonitor.left, 0, workW - w);
    const int y = here.rcMonitor.top + std::clamp<int>(normal.top - was.rcMonitor.top, 0, workH - h);
    wp.rcNormalPosition = {x, y, x + w, y + h};
    wp.showCmd = SW_SHOWMAXIMIZED;
    SetWindowPlacement(hwnd, &wp);   // still maximised: nothing moves
}

void MainWindow::fitLibraryHead()
{
    // Zoomed in, or with the panel narrow, the chips with their counts and
    // the title were wider than the panel and ran over each other.
    if (!m_chips || !m_libraryPanel)
        return;
    const QMargins m = m_libraryPanel->layout()->contentsMargins();
    const int room = m_libraryPanel->width() - m.left() - m.right();
    static const char *names[] = {"All", "Songs", "Videos"};
    // Without their counts the chips are as narrow as they go; the panel
    // goes covers-only below that (they were squashed to "ong", "de").
    for (int i = 0; i < 3; ++i)
        m_chips->button(i)->setText(QString::fromLatin1(names[i]));
    m_chipsWidth = m_chipRow->layout()->sizeHint().width() + m.left() + m.right() + 8;
    for (bool counts : {true, false}) {
        for (int i = 0; i < 3; ++i) {
            QAbstractButton *chip = m_chips->button(i);
            const QVariant count = chip->property("count");
            chip->setText(counts && count.isValid() ? QStringLiteral("%1 %2").arg(names[i]).arg(count.toInt())
                                                    : QString::fromLatin1(names[i]));
        }
        if (m_chipRow->layout()->sizeHint().width() <= room)
            break;
    }
    int buttons = 0;
    for (QWidget *w : std::initializer_list<QWidget *>{m_collapse, m_libraryFind, m_viewToggle, m_addButton})
        buttons += w->sizeHint().width() + m_libraryHead->layout()->spacing();
    m_libraryTitle->setText(m_libraryTitle->fontMetrics().elidedText("Your Library", Qt::ElideRight, qMax(0, room - buttons)));
}

int MainWindow::compactBelow() const
{
    return qMax(kCompactBelow, m_chipsWidth);
}

void MainWindow::syncSearchWidth()
{
    // The search box spans the library panel below it, edge to edge.
    if (!m_search || !m_split)
        return;
    const int panel = m_split->sizes().value(0);
    // Less the Home button (34 px at 100%, as in the style sheet) and its gap.
    const qreal z = theme::chromeZoom();
    int width = qMax(int(220 * z), panel - qRound(34 * z) - 8);
    // And no wider than the bar has room for beside its buttons: zoomed in a
    // narrow window it ran under Home and the profile picture.
    if (m_topBar) {
        const auto *row = m_topBar->layout();
        int others = row->contentsMargins().left() + row->contentsMargins().right() + 10;
        for (QWidget *w : std::as_const(m_chromeButtons))
            if (w != m_search && w != m_tabStrip && w->isVisible())
                others += w->sizeHint().width() + row->spacing();
        width = qMin(width, m_topBar->width() - others - 8);
    }
    m_search->setFixedWidth(qMax(120, width));
}

void MainWindow::openPlayingList()
{
    // A video list goes to its watch page, also while mpv plays it in its own
    // window: that page is where it is brought back from.
    if ((m_mpvEmbedded || m_videoPoppedOut) && m_mpv.connected() &&
        (!m_mpv.property("user-data/ghost/audio").toBool() || songVideo())) {
        // A YouTube video seen from the music layout: its page belongs to
        // the YouTube layout, so the switch comes first (Batın, 2026-11-08:
        // the page opened among the music menus). A song's video stays.
        const bool youtubeVideo = !m_mpv.property("user-data/ghost/audio").toBool();
        if (youtubeVideo && !m_youtubeLayout) {
            setYouTubeLayout(true, true, [this] { showVideoPage(); });
            return;
        }
        showVideoPage();
        return;
    }
    QString playing = playingListPath();
    int at = -1;
    if (playing.isEmpty() && !m_mpv.connected()) {
        // Just started: the bar shows the song kept from the last run, and
        // mpv is not there to say its list until play (2026-11-11).
        QSettings settings;
        playing = settings.value("resume/list").toString();
        if (!playing.isEmpty() && QFileInfo::exists(playing))
            playing = QFileInfo(playing).absoluteFilePath();
        else
            playing.clear();
        at = settings.value("resume/pos", -1).toInt();
    }
    const auto &all = m_library.playlists();
    for (int i = 0; i < all.size(); ++i) {
        if (QFileInfo(all[i].path).absoluteFilePath().compare(playing, Qt::CaseInsensitive) != 0)
            continue;
        openPlaylist(i);
        if (m_playingRow >= 0)
            QTimer::singleShot(0, this, [this] {
                m_songs->scrollToItem(m_songs->item(m_playingRow), QAbstractItemView::PositionAtCenter);
            });
        return;
    }
    // Played from a search (a song's radio, "1 of 50"): its list as a page,
    // the song playing marked, to be kept as "Ghost Engine Radio #n"
    // (Batın, 2026-11-09).
    if (!playing.isEmpty() &&
        QDir::cleanPath(QFileInfo(playing).absolutePath()).compare(foundFolder(), Qt::CaseInsensitive) == 0)
        showRadioList(playing, at);
}

QString MainWindow::playingTarget() const
{
    const QString source = m_mpv.property("user-data/ghost/source").toString();
    const QString id = youtube::videoId(source.isEmpty() ? m_mpv.property("path").toString() : source);
    return id.isEmpty() ? QString() : QStringLiteral("https://www.youtube.com/watch?v=") + id;
}

QString MainWindow::likedPath(bool make)
{
    QSettings settings;
    QString path = settings.value("liked/path").toString();
    if (!path.isEmpty() && QFileInfo::exists(path))
        return path;
    if (!make)
        return {};
    path = m_library.createPlaylist(QStringLiteral("Liked songs"), QString());
    if (!path.isEmpty())
        settings.setValue("liked/path", path);
    m_likedKnown = false;   // its cover follows
    return path;
}

void MainWindow::toggleLiked()
{
    const auto [artist, track] = playingSong();
    toggleLiked(playingTarget(), track.isEmpty() ? m_mpv.property("media-title").toString().trimmed()
                                                 : (artist.isEmpty() ? track : artist + QStringLiteral(" - ") + track));
}

void MainWindow::markLikedRows()
{
    QSet<QString> liked;
    const QString path = likedPath(false);
    if (!path.isEmpty())
        for (const PlaylistEntry &e : Playlist::peek(path).readEntries())
            liked.insert(youtube::videoId(e.target));
    for (int row = 0; row < m_songs->count(); ++row) {
        QListWidgetItem *item = m_songs->item(row);
        item->setData(tiles::kLikedRole, liked.contains(idForTarget(item->data(Qt::UserRole).toString())));
    }
}

void MainWindow::toggleLiked(const QString &given, const QString &title)
{
    // A song line found by search (ytdl://ytsearch1:...) is kept as the
    // video it resolved to.
    QString id = youtube::videoId(given);
    if (id.isEmpty())
        id = idForTarget(given);
    if (id.isEmpty()) {
        toast("Play the song once, then like it.");
        return;
    }
    const QString target = QStringLiteral("https://www.youtube.com/watch?v=") + id;
    const QString path = likedPath(true);
    if (path.isEmpty()) {
        toast("Liked songs couldn't be made.");
        return;
    }
    const QVector<PlaylistEntry> entries = Playlist::peek(path).readEntries();
    QVector<int> keep;
    for (int i = 0; i < entries.size(); ++i)
        if (youtube::videoId(entries[i].target) != id)
            keep << i;
    bool ok;
    if (keep.size() < entries.size()) {
        ok = Playlist::writeOrder(path, keep);
        if (ok)
            toast("Removed from Liked songs");
    } else {
        ok = Playlist::append(path, {PlaylistEntry{title, target, {}}});
        if (ok)
            toast("Added to Liked songs");
    }
    if (!ok) {
        toast("Liked songs couldn't be written.");
        return;
    }
    // The library shows the new count; the page open stays open (Liked songs
    // too, with the change), where it was.
    rescan();
    markLikedRows();
    m_nowPlaying->refreshLike();
}

void MainWindow::showRadioList(const QString &path, int at)
{
    const Playlist list = Playlist::peek(path);
    const QVector<PlaylistEntry> entries = list.readEntries();
    QVector<ytmusic::Item> songs;
    for (const PlaylistEntry &e : entries) {
        ytmusic::Item it;
        it.kind = list.audio ? ytmusic::Kind::Song : ytmusic::Kind::Video;
        it.id = youtube::videoId(e.target);
        // "<artist> - <title>", as the line was written.
        const int dash = e.title.indexOf(QStringLiteral(" - "));
        it.artist = dash > 0 ? e.title.left(dash).trimmed() : QString();
        it.title = dash > 0 ? e.title.mid(dash + 3).trimmed() : e.title.trimmed();
        const int seconds = m_resolver.durationFor(it.id);
        it.duration = seconds > 0 ? QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'))
                                  : QStringLiteral(" ");
        if (!it.id.isEmpty())
            songs << it;
    }
    int n = 1;
    for (const Playlist &p : m_library.playlists()) {
        static const QRegularExpression numbered(QStringLiteral(R"(^Ghost Engine Radio #(\d+)$)"));
        const auto m = numbered.match(p.name);
        if (m.hasMatch())
            n = qMax(n, m.captured(1).toInt() + 1);
    }
    const QString title = list.name.isEmpty() ? QStringLiteral("Radio") : list.name;
    if (at < 0)
        at = m_mpv.property("playlist-pos").toInt();
    if (m_pages->currentWidget() != m_searchPage) {
        QWidget *now = m_pages->currentWidget();
        m_beforeSearch = now == m_videoPage || now == m_lyricsView ? m_pages->widget(0) : now;
    }
    m_pages->setCurrentWidget(m_searchPage);
    m_searchPage->showLocalList(title, QStringLiteral("Radio  •  %1 songs, playing %2").arg(songs.size()).arg(at + 1),
                                songs, at, QStringLiteral("Ghost Engine Radio #%1").arg(n));
}

void MainWindow::goToArtist(const QString &artists)
{
    // "A & B", "A, B & C", "A x B", "A feat. B": the first one's page.
    static const QRegularExpression between(QStringLiteral(R"(\s*(?:,|&| x | X |\bfeat\.?|\bft\.?|\bwith\b)\s*)"));
    const QString name = artists.split(between, Qt::SkipEmptyParts).value(0).trimmed();
    if (name.isEmpty())
        return;
    if (m_pages->currentWidget() != m_searchPage) {
        QWidget *now = m_pages->currentWidget();
        m_beforeSearch = now == m_videoPage || now == m_lyricsView ? m_pages->widget(0) : now;
    }
    m_pages->setCurrentWidget(m_searchPage);
    m_searchPage->openArtistNamed(name);
}

void MainWindow::chooseCover(int index)
{
    const auto &all = m_library.playlists();
    if (index < 0 || index >= all.size())
        return;
    const QString picked = QFileDialog::getOpenFileName(this, "Choose a cover",
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
        "Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)");
    if (picked.isEmpty())
        return;
    const QImage picture(picked);
    if (picture.isNull()) {
        QMessageBox::warning(this, "Choose a cover", "That picture couldn't be read.");
        return;
    }
    // The user picks the square; it is kept no bigger than the largest
    // place a cover is drawn.
    CoverCropDialog crop(picture, this);
    if (crop.exec() != QDialog::Accepted)
        return;
    QImage image = crop.cropped();
    const int side = qMin(image.width(), 640);
    image = image.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QString target = customCoverPath(all[index].path);
    QDir().mkpath(QFileInfo(target).absolutePath());
    if (!image.save(target, "JPG", 92)) {
        QMessageBox::warning(this, "Choose a cover", "The cover couldn't be saved.");
        return;
    }
    m_customCovers.remove(target);
    for (QListWidget *v : {m_libraryList, m_recentAdded, m_recentPlayed, m_topLists})
        v->viewport()->update();
    m_playlistCover->update();
}

void MainWindow::removeCover(int index)
{
    const auto &all = m_library.playlists();
    if (index < 0 || index >= all.size())
        return;
    const QString target = customCoverPath(all[index].path);
    QFile::remove(target);
    m_customCovers.remove(target);
    for (QListWidget *v : {m_libraryList, m_recentAdded, m_recentPlayed, m_topLists})
        v->viewport()->update();
    m_playlistCover->update();
}

void MainWindow::showSharedBy(const QString &tag)
{
    // "Shared by srew  ·  listens to BEGE, Jako, Lil Zey", with their avatar.
    const QJsonObject who = QJsonDocument::fromJson(tag.toUtf8()).object();
    const QString name = who.value("name").toString();
    if (name.isEmpty()) {
        m_sharedBy->hide();
        return;
    }
    QString text = QStringLiteral("Shared by <b>%1</b>").arg(name.toHtmlEscaped());
    QStringList top;
    for (const QJsonValue &v : who.value("top").toArray())
        top << v.toString().toHtmlEscaped();
    if (!top.isEmpty())
        text += QStringLiteral("  ·  listens to ") + top.join(QStringLiteral(", "));
    m_sharedText->setText(text);
    m_sharedText->setToolTip(who.value("handle").toString().isEmpty() ? QString()
                                 : QStringLiteral("@") + who.value("handle").toString());
    m_sharedAvatarUrl = who.value("avatar").toString();
    const QPixmap avatar = m_pictures.get(m_sharedAvatarUrl);
    const qreal dpr = devicePixelRatioF();
    QPixmap round(QSize(22, 22) * dpr);
    round.setDevicePixelRatio(dpr);
    round.fill(Qt::transparent);
    QPainter p(&round);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath circle;
    circle.addEllipse(QRectF(0, 0, 22, 22));
    p.setClipPath(circle);
    if (avatar.isNull())
        p.fillRect(QRectF(0, 0, 22, 22), theme::c().raisedHover);
    else
        p.drawPixmap(QRectF(0, 0, 22, 22), avatar, QRectF(avatar.rect()));
    p.end();
    m_sharedAvatar->setPixmap(round);
    m_sharedBy->setVisible(m_playlistCover->height() > 150 * theme::zoom());
}

void MainWindow::shareList(int index)
{
    // A copy of the list with who shares it written in (name, avatar, the
    // artists they play most), put on the clipboard as a file, the way
    // Explorer copies one: Ctrl+V in a Discord chat sends it. Whoever opens
    // it gets it in their library with the card under its name.
    const auto &all = m_library.playlists();
    if (index < 0 || index >= all.size())
        return;
    QFile source(all[index].path);
    if (!source.open(QIODevice::ReadOnly)) {
        toast(QStringLiteral("The playlist couldn't be read"));
        return;
    }
    const QString content = QString::fromUtf8(source.readAll());
    source.close();
    const QString eol = content.contains(QStringLiteral("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
    QStringList lines = content.split(QRegularExpression(QStringLiteral("\r?\n")));
    lines.erase(std::remove_if(lines.begin(), lines.end(), [](const QString &l) {
        return l.trimmed().startsWith(QStringLiteral("#GHOST_SHARED:"));   // a friend's card goes, ours comes
    }), lines.end());

    QJsonObject who;
    who["name"] = m_profile.known() ? m_profile.name() : qEnvironmentVariable("USERNAME");
    if (m_profile.known()) {
        who["handle"] = m_profile.handle();
        who["id"] = m_profile.id();
        who["avatar"] = m_profile.avatarUrl();
    }
    QJsonArray top;
    for (const auto &a : m_stats.topArtists(3))
        top.append(a.name);
    if (!top.isEmpty())
        who["top"] = top;
    who["at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString tag = QStringLiteral("#GHOST_SHARED:") + QString::fromUtf8(QJsonDocument(who).toJson(QJsonDocument::Compact));

    // After the header (#EXTM3U, #PLAYLIST:, #GHOST_AUDIO): #GHOST_AUDIO is
    // looked for in the first bytes only.
    int at = 0;
    while (at < lines.size() && lines[at].trimmed().startsWith('#') &&
           !lines[at].trimmed().startsWith(QStringLiteral("#EXTINF")) &&
           !lines[at].trimmed().startsWith(QStringLiteral("#GHOST_ADDED")))
        ++at;
    lines.insert(at, tag);

    static const QRegularExpression forbidden(QStringLiteral(R"([\\/:*?"<>|]+)"));
    QString clean = all[index].name;
    clean.replace(forbidden, QStringLiteral(" ")).replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    clean = clean.trimmed();
    if (clean.isEmpty())
        clean = QStringLiteral("playlist");
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/Ghost Engine share");
    QDir().mkpath(folder);
    const QString target = folder + '/' + clean + QStringLiteral(".m3u");
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(lines.join(eol).toUtf8()) < 0) {
        toast(QStringLiteral("The copy to share couldn't be written"));
        return;
    }
    out.close();

    auto *mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(target)});
    // Explorer's own hint that this is a copy, not a move.
    mime->setData(QStringLiteral("application/x-qt-windows-mime;value=\"Preferred DropEffect\""),
                  QByteArray("\x01\x00\x00\x00", 4));
    QGuiApplication::clipboard()->setMimeData(mime);
    toast(QStringLiteral("Copied %1. Paste it into a Discord chat with Ctrl+V").arg(all[index].name));
}

void MainWindow::exportPlaylist(int index)
{
    // A copy to share, under the list's name with what Windows refuses in
    // file names taken out; whoever imports it gets their own random key.
    const auto &all = m_library.playlists();
    if (index < 0 || index >= all.size())
        return;
    static const QRegularExpression forbidden(QStringLiteral(R"([\\/:*?"<>|]+)"));
    QString clean = all[index].name;
    clean.replace(forbidden, QStringLiteral(" ")).replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    clean = clean.trimmed();
    if (clean.isEmpty())
        clean = QStringLiteral("playlist");
    const QString target = QFileDialog::getSaveFileName(this, "Export a copy",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + '/' + clean + ".m3u", "Playlists (*.m3u)");
    if (target.isEmpty())
        return;
    QFile::remove(target);
    if (!QFile::copy(all[index].path, target))
        QMessageBox::warning(this, "Export a copy", "The copy couldn't be written there.");
}

void MainWindow::changeZoom(qreal step)
{
    // Wheel steps gathered and applied once the wheel rests: each step
    // restyled and laid out every page, and a quick turn of the wheel froze
    // the app until Windows called it not responding (Batin, 2026-11-20).
    const qreal from = m_zoomSoon && m_zoomSoon->isActive() ? m_zoomTarget : theme::zoom();
    m_zoomTarget = step == 0 ? 1.0 : qBound(0.7, qRound((from + step) * 10) / 10.0, 1.6);
    if (!m_zoomSoon) {
        m_zoomSoon = new QTimer(this);
        m_zoomSoon->setSingleShot(true);
        m_zoomSoon->setInterval(150);
        connect(m_zoomSoon, &QTimer::timeout, this, [this] { applyZoom(m_zoomTarget); });
    }
    m_zoomSoon->start();
}

void MainWindow::applyZoom(qreal zoom)
{
    if (qFuzzyCompare(zoom, theme::zoom()))
        return;
    theme::setZoom(*static_cast<QApplication *>(QCoreApplication::instance()), zoom);
    QSettings().setValue("window/zoom", theme::zoom());
    for (QListWidget *v : {m_libraryList, m_recentAdded, m_recentPlayed, m_topLists, m_songs})
        v->doItemsLayout();
    for (QListWidget *v : {m_recentAdded, m_recentPlayed, m_topLists})
        v->setFixedHeight(int((kTile + 70 + 26) * theme::zoom()));
    if (m_panel)
        m_panel->setMinimumWidth(qRound(340 * theme::zoom()));
    m_topBar->setFixedHeight(qRound(kTopBar * theme::chromeZoom()));
    m_libraryPanel->setMinimumWidth(compactWidth());
    if (m_compact) {
        const int total = m_split->sizes().value(0) + m_split->sizes().value(1);
        m_split->setSizes({compactWidth(), total - compactWidth()});
    }
    if (m_playlistCover)
        foldHeader();
    syncSearchWidth();
    fitLibraryHead();
    layoutHeader();
    if (m_settings)
        m_settings->showZoom(theme::zoom());
}

void MainWindow::tieToApp(qint64 pid)
{
    // An mpv playing inside the app ends with it, also when the app does not
    // get to say so (a crash, or ended from Task Manager): before, it played
    // on with nothing left to stop it from. Its own children (yt-dlp, Ghost
    // RPC) go with it. One in its own window is left out and plays on, as
    // it does when the app is closed.
    if (!m_playerJob) {
        m_playerJob = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (m_playerJob)
            SetInformationJobObject(m_playerJob, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
    if (HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(pid))) {
        if (m_playerJob)
            AssignProcessToJobObject(m_playerJob, process);
        CloseHandle(process);
    }
}

QString MainWindow::memoryUsage() const
{
    // As Task Manager's Memory column counts it (private working set): the
    // app, mpv, and what mpv started (Ghost RPC for Discord).
    const auto privateMb = [](DWORD pid) -> double {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h)
            return 0;
        PROCESS_MEMORY_COUNTERS_EX2 counters{};
        counters.cb = sizeof(counters);
        double mb = 0;
        if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)))
            mb = counters.PrivateWorkingSetSize / (1024.0 * 1024.0);
        CloseHandle(h);
        return mb;
    };
    const double app = privateMb(GetCurrentProcessId());
    const DWORD mpv = m_mpv.connected() ? DWORD(m_mpv.property("pid").toLongLong()) : 0;
    double player = mpv ? privateMb(mpv) : 0, helpers = 0;
    if (mpv) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W entry{sizeof(entry)};
        for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
            if (entry.th32ParentProcessID == mpv)
                helpers += privateMb(entry.th32ProcessID);
        CloseHandle(snapshot);
    }
    QString text = QStringLiteral("About %1 MB: the app %2 MB").arg(qRound(app + player + helpers)).arg(qRound(app));
    if (mpv)
        text += QStringLiteral(", the player %1 MB").arg(qRound(player));
    if (helpers > 0.5)
        text += QStringLiteral(", Discord status %1 MB").arg(qRound(helpers));
    return text + QStringLiteral(".");
}

void MainWindow::markPlaying()
{
    // The song mpv is on, when the list open here is the one it plays.
    const auto &all = m_library.playlists();
    const bool same = m_open >= 0 && m_open < all.size() && m_mpv.connected() &&
        playingListPath()
            .compare(QFileInfo(all[m_open].path).absoluteFilePath(), Qt::CaseInsensitive) == 0;
    // Matched by the entry itself, not its position: shuffled, mpv's order
    // is no longer the file's.
    const QString current = same ? m_mpv.property("path").toString() : QString();
    int playing = -1;
    for (int row = 0; row < m_songs->count(); ++row) {
        const bool on = !current.isEmpty() && playing < 0 &&
                        m_songs->item(row)->data(Qt::UserRole).toString() == current;
        if (on)
            playing = row;
        m_songs->item(row)->setData(tiles::kPlayingRole, on);
    }
    m_playingRow = playing;
    if (m_nowPlaying)
        m_nowPlaying->refreshPosition();   // its number may be this row now
}

void MainWindow::toggleLyrics()
{
    // The microphone opens the lyrics and, pressed again, goes back to the
    // page that was open, as Spotify's does.
    if (m_pages->currentWidget() == m_lyricsView) {
        m_pages->setCurrentWidget(m_beforeLyrics ? m_beforeLyrics : m_pages->widget(0));
        m_nowPlaying->setLyricsOpen(false);
        return;
    }
    m_beforeLyrics = m_pages->currentWidget();
    if (m_lyricsKey.isEmpty())
        lookUpLyrics();
    m_lyricsView->setPosition(m_mpv.property("time-pos").toDouble());
    m_pages->setCurrentWidget(m_lyricsView);
    m_nowPlaying->setLyricsOpen(true);
}

std::pair<QString, QString> MainWindow::playingSong() const
{
    // The list's own "Artist - Track" is cleaner than a YouTube title; the
    // title is the fallback for a song opened on its own.
    QString title;
    const QString list = playingListPath();
    const QString path = m_mpv.property("path").toString();
    if (!list.isEmpty()) {
        Playlist p;
        p.path = list;
        for (const PlaylistEntry &e : p.readEntries())
            if (e.target == path) { title = e.title; break; }
    }
    if (title.isEmpty())
        title = m_mpv.property("media-title").toString();
    auto [track, artist] = splitTitle(title);
    if (artist.isEmpty())
        artist = m_mpv.property("metadata/by-key/uploader").toString().remove(QStringLiteral(" - Topic"));
    // What a lyrics site does not have in its titles: "(Official Video)",
    // "[Lyrics]", "- Super Slowed", "ft. Someone"; and only the first of
    // several artists.
    static const QRegularExpression noise(QStringLiteral(
        R"(\s*[\(\[][^\)\]]*(official|video|lyric|audio|visuali[sz]er|clip|klip|hd|4k|prod|slowed|sped|speed|reverb|nightcore|remix)[^\)\]]*[\)\]])"
        R"(|\s+-\s+((super|ultra)\s+)?(slowed|sped up|speed up|reverb|nightcore).*$)"
        R"(|\s+(ft\.?|feat\.?)\s+.*$)"
        R"(|\s+(\||//)\s+.*$)"), QRegularExpression::CaseInsensitiveOption);   // "NAZAR | Prod. By Bugy"
    track.remove(noise);
    artist = artist.split(QRegularExpression(QStringLiteral(R"(\s*(,|&| x | X |feat\.?|ft\.?)\s*)"))).value(0);
    return {artist.trimmed(), track.trimmed()};
}

void MainWindow::lookUpLyrics()
{
    if (!m_mpv.connected())
        return;
    const auto [artist, track] = playingSong();
    if (track.isEmpty())
        return;
    const QString key = LyricsSource::keyFor(artist, track);
    if (key == m_lyricsKey)
        return;
    m_lyricsKey = key;
    m_lyrics = Lyrics();
    m_panel->setLyrics(m_lyrics, true);
    m_lyricsView->setSearching(artist.isEmpty() ? track : artist + QStringLiteral(" · ") + track);
    m_lyricsView->setSearchText((artist + ' ' + track).trimmed());
    m_lyricsSource.fetch(artist, track, m_mpv.property("duration").toDouble());
}

void MainWindow::tintLyrics()
{
    // The page takes the playing song's cover colour, kept dark enough for
    // white words.
    QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString());
    if (id.isEmpty())
        id = youtube::videoId(m_mpv.property("path").toString());
    const QPixmap cover = m_covers.get(id, ThumbnailCache::Size::Card);
    if (cover.isNull()) {
        m_lyricsView->setTint(QColor());
        return;
    }
    const QColor avg = cover.toImage().scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixelColor(0, 0);
    m_lyricsView->setTint(QColor::fromHsv(avg.hsvHue(), qMin(avg.hsvSaturation(), 170), qBound(60, avg.value(), 110)));
}

void MainWindow::showShortcuts()
{
    m_open = -1;
    m_libraryList->clearSelection();
    m_pages->setCurrentWidget(m_shortcutsScroll);
    m_nowPlaying->setLyricsOpen(false);
}

void MainWindow::runShortcut(const QString &action)
{
    const bool playing = m_mpv.connected();
    if (action == QLatin1String("playPause") && playing && !m_mpv.property("user-data/ghost/audio").toBool())
        togglePause();   // a video: at its end, from its start again
    else if (action == QLatin1String("playPause") || action == QLatin1String("shuffle") || action == QLatin1String("repeat"))
        m_nowPlaying->press(action);
    else if (action == QLatin1String("next") && playing)
        m_mpv.command({"playlist-next"});
    else if (action == QLatin1String("previous") && playing)
        m_mpv.command({"playlist-prev"});
    else if (action == QLatin1String("forward") && playing)
        m_mpv.command({"seek", icons::skipSeconds(), "relative+exact"});
    else if (action == QLatin1String("back") && playing)
        m_mpv.command({"seek", -icons::skipSeconds(), "relative+exact"});
    else if (action == QLatin1String("volumeUp") && playing)
        m_mpv.command({"add", "volume", 5});
    else if (action == QLatin1String("volumeDown") && playing)
        m_mpv.command({"add", "volume", -5});
    else if (action == QLatin1String("mute") && playing)
        m_mpv.command({"cycle", "mute"});
    else if (action == QLatin1String("subtitles") && playing)
        m_mpv.command({"cycle", "sub"});
    else if (action == QLatin1String("fullScreen")) {
        if (m_videoFull || m_pages->currentWidget() == m_videoPage)
            setVideoFullScreen(!m_videoFull);
    } else if (action == QLatin1String("cinema")) {
        if (!m_videoFull && m_pages->currentWidget() == m_videoPage && m_videoPage->onCinema)
            m_videoPage->onCinema();
    } else if (action == QLatin1String("mini")) {
        if (m_videoPage->onMinimize && m_pages->currentWidget() == m_videoPage)
            m_videoPage->onMinimize();
    } else if (action == QLatin1String("search")) {
        // The Search shortcut (Ctrl+K unless changed) opens the middle box either way.
        if (m_videoFull)
            setVideoFullScreen(false);
        openSearchPopup();
    } else if (action == QLatin1String("goBack") || action == QLatin1String("goForward")) {
        goHistory(action == QLatin1String("goForward"));
    } else if (action == QLatin1String("librarySearch")) {
        if (m_compact)
            m_collapse->click();
        m_libraryFind->click();
    } else if (action == QLatin1String("home"))
        showHome();
    else if (action == QLatin1String("lyrics"))
        toggleLyrics();
    else if (action == QLatin1String("settings"))
        showSettings();
    else if (action == QLatin1String("shortcuts"))
        showShortcuts();
}

void MainWindow::showSettings()
{
    m_open = -1;
    m_libraryList->clearSelection();
    m_settings->load();
    m_pages->setCurrentWidget(m_settings);
    m_nowPlaying->setLyricsOpen(false);
}

void MainWindow::applyLook()
{
    QSettings settings;
    tiles::setCircleCovers(settings.value("look/circleCovers").toBool());
    m_nowPlaying->setVolumeNumber(settings.value("look/volumeNumber", false).toBool());
    m_nowPlaying->refreshCover();
    for (QListWidget *v : {m_libraryList, m_recentAdded, m_recentPlayed, m_topLists})
        v->viewport()->update();
    m_playlistCover->update();
}

void MainWindow::sendWindowChoice()
{
    // Only to an mpv the app talks to: an mpv opened from Explorer keeps
    // its window, so a song list there can always be stopped.
    if (m_mpv.connected())
        m_mpv.command({"change-list", "script-opts", "append",
                       QSettings().value("player/window", false).toBool() ? "ghost-window=yes" : "ghost-window=no"});
}

void MainWindow::showProfile()
{
    m_open = -1;
    m_libraryList->clearSelection();
    m_profile.refresh();   // a new avatar or name since the last look
    refreshProfile();
    m_pages->setCurrentWidget(m_profileScroll);
    m_nowPlaying->setLyricsOpen(false);
}

void MainWindow::refreshProfileUser()
{
    // Discord's name and picture; before Discord has ever answered, the
    // Windows user's name.
    const bool known = m_profile.known();
    const QString name = known ? m_profile.name() : qEnvironmentVariable("USERNAME");
    const QString counts = QStringLiteral("%1 playlists  ·  %2 songs and videos").arg(m_statLists->text(), m_statSongs->text());
    m_profilePage->setUser(name, known ? m_profile.handle() : QString(),
                           m_profile.avatar(), known ? counts : counts + QStringLiteral("  ·  Open Discord to show your profile"));

    // The same face, small, at the top left.
    const qreal dpr = devicePixelRatioF();
    QPixmap icon(QSize(28, 28) * dpr);
    icon.setDevicePixelRatio(dpr);
    icon.fill(Qt::transparent);
    QPainter p(&icon);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath circle;
    circle.addEllipse(QRectF(0, 0, 28, 28));
    p.setClipPath(circle);
    const QPixmap avatar = m_profile.avatar();
    if (avatar.isNull()) {
        p.fillRect(QRectF(0, 0, 28, 28), theme::c().raisedHover);
        QFont icons(QStringLiteral("Segoe Fluent Icons"));
        icons.setPixelSize(14);
        p.setFont(icons);
        p.setPen(theme::c().textSoft);
        p.drawText(QRectF(0, 0, 28, 28), Qt::AlignCenter, QString(QChar(0xE77B)));
    } else {
        const int edge = qMin(avatar.width(), avatar.height());
        p.drawPixmap(QRectF(0, 0, 28, 28), avatar,
                     QRect((avatar.width() - edge) / 2, (avatar.height() - edge) / 2, edge, edge));
    }
    p.end();
    m_profileButton->setIcon(QIcon(icon));
    m_profileButton->setToolTip(name.isEmpty() ? QStringLiteral("Profile") : name);
    if (m_ytProfile) {
        m_ytProfile->setIcon(QIcon(icon));
        m_ytProfile->setText(m_ytCompact ? QString() : m_profileButton->toolTip());
    }
}

void MainWindow::saveResume()
{
    if (!m_mpv.connected())
        return;
    const QString list = playingListPath();
    const QString path = m_mpv.property("path").toString();
    if (list.isEmpty() || path.isEmpty() || !QFileInfo::exists(list))
        return;
    auto [artist, track] = playingSong();
    if (track.isEmpty())
        track = m_mpv.property("media-title").toString();
    QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString());
    if (id.isEmpty())
        id = idForTarget(path);
    // The place in the list's own file: queued songs before it do not count.
    // A queued song or one Autoplay added is in no file: what is kept is
    // the list's own song that comes next, so Play goes on with the list
    // rather than not saving at all (the bar showed a list from an hour
    // before, 2026-11-11).
    const QVariantList entries = m_mpv.property("playlist").toList();
    const int at = m_mpv.property("playlist-pos").toInt();
    int filePos = at;
    for (int i = 0; i < qMin(at, int(entries.size())); ++i)
        if (m_queueIds.contains(entries[i].toMap().value("id").toLongLong()))
            --filePos;
    const QVector<PlaylistEntry> own = Playlist::peek(list).readEntries();
    double second = m_mpv.property("time-pos").toDouble();
    if ((playingQueued() || filePos >= own.size()) && !own.isEmpty()) {
        second = 0;
        filePos = qBound(0, filePos, int(own.size()) - 1);
        const PlaylistEntry &e = own[filePos];
        const auto [t, a] = splitTitle(e.title);
        track = t.isEmpty() ? e.title : t;
        artist = a;
        id = idForTarget(e.target);
    }
    QSettings settings;
    settings.setValue("resume/list", list);
    settings.setValue("resume/pos", filePos);
    settings.setValue("resume/at", second);
    settings.setValue("resume/duration", m_mpv.property("duration").toDouble());
    settings.setValue("resume/title", track);
    settings.setValue("resume/artist", artist);
    settings.setValue("resume/id", id);
    // A video goes on from its second; a song starts again from the top
    // (Batın: a song heard from its middle is not much of a song).
    settings.setValue("resume/video", !m_mpv.property("user-data/ghost/audio").toBool());
}

void MainWindow::showResume()
{
    QSettings settings;
    const QString list = settings.value("resume/list").toString();
    if (!settings.value("playback/resume", true).toBool() || list.isEmpty() || !QFileInfo::exists(list)) {
        m_nowPlaying->setResume({}, {}, {}, 0, 0);
        return;
    }
    const QString artist = settings.value("resume/artist").toString();
    const QString name = Playlist::peek(list).name;
    const bool video = settings.value("resume/video").toBool();
    m_nowPlaying->setResume(settings.value("resume/title").toString(),
                            artist.isEmpty() ? name : artist + QStringLiteral("  ·  ") + name,
                            settings.value("resume/id").toString(), video ? settings.value("resume/at").toDouble() : 0,
                            settings.value("resume/duration").toDouble());
}

void MainWindow::resume()
{
    // The same list from the same song: a video from its second, a song
    // from the top. Shuffle starts afresh.
    QSettings settings;
    const QString list = settings.value("resume/list").toString();
    if (list.isEmpty() || !QFileInfo::exists(list))
        return;
    m_startSeconds = settings.value("resume/video").toBool() ? settings.value("resume/at").toDouble() : 0;
    play(list, false, settings.value("resume/pos").toInt());
}

void MainWindow::refreshProfile()
{
    refreshProfileUser();
    if (!m_videosSorted) {
        // Before 1.3.1 every play counted as a song: the ones whose video is
        // in a video list were videos, and go over to Top videos.
        m_videosSorted = true;
        QSet<QString> ids;
        for (const Playlist &p : m_library.playlists())
            if (!p.audio)
                for (const PlaylistEntry &e : p.readEntries())
                    if (const QString id = youtube::videoId(e.target); !id.isEmpty())
                        ids.insert(id);
        m_stats.sortOutVideos(ids);
    }

    // What friends see on Discord.
    const QString path = m_mpv.connected() ? m_mpv.property("path").toString() : QString();
    if (path.isEmpty()) {
        m_profilePage->setIdle();
    } else {
        const auto [artist, track] = playingSong();
        QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString());
        if (id.isEmpty())
            id = idForTarget(path);
        const QString list = playingListPath();
        m_profilePage->setListening(m_covers.get(id, ThumbnailCache::Size::Card),
                                    track.isEmpty() ? m_mpv.property("media-title").toString() : track, artist,
                                    list.isEmpty() ? QString() : Playlist::peek(list).name,
                                    m_mpv.property("pause").toBool());
        m_profilePage->setPosition(m_mpv.property("time-pos").toDouble(), m_mpv.property("duration").toDouble());
    }

    QList<ProfilePage::Artist> artists;
    for (const auto &a : m_stats.topArtists(6)) {
        const ArtistInfo *info = m_artists.known(ArtistLookup::keyFor(a.name));
        if (!info)
            m_artists.fetch(a.name, QString());   // the photo, once found
        artists.append({a.name, info ? info->photo : QPixmap(), a.plays});
    }
    m_profilePage->setTopArtists(artists);

    QList<ProfilePage::Song> songs;
    for (const auto &s : m_stats.topSongs(5))
        songs.append({s.title, s.artist, m_covers.get(s.id, ThumbnailCache::Size::Card), s.plays});
    m_profilePage->setTopSongs(songs);
    QList<ProfilePage::Song> videos;
    for (const auto &s : m_stats.topVideos(5))
        videos.append({s.title, s.artist, m_covers.get(s.id, ThumbnailCache::Size::Card), s.plays});
    m_profilePage->setTopVideos(videos);

    QList<ProfilePage::Played> plays;
    const QList<ListeningStats::Entry> history = m_stats.history();
    const QDateTime now = QDateTime::currentDateTime();
    for (const auto &e : history.mid(0, m_historyShown)) {
        const qint64 ago = e.at.secsTo(now);
        const QString when = ago < 60 ? QStringLiteral("just now")
                           : ago < 3600 ? QStringLiteral("%1 min ago").arg(ago / 60)
                           : ago < 86400 ? QStringLiteral("%1 h ago").arg(ago / 3600)
                           : ago < 2 * 86400 ? QStringLiteral("yesterday")
                           : QLocale().toString(e.at.date(), QLocale::ShortFormat);
        const QString list = e.list.isEmpty() ? QString() : Playlist::peek(e.list).name;
        const QString sub = e.artist.isEmpty() ? list
                          : list.isEmpty() ? e.artist : e.artist + QStringLiteral("  ·  ") + list;
        plays.append({e.title, sub, when, m_covers.get(e.id, ThumbnailCache::Size::Card), e.video});
    }
    m_profilePage->setHistory(plays, history.size() > m_historyShown, ListeningStats::keepsHistory());

    const auto &all = m_library.playlists();
    QVector<int> lists;
    for (const auto &l : m_stats.topLists(kShelfCount))
        for (int i = 0; i < all.size(); ++i)
            if (all[i].path.compare(l.name, Qt::CaseInsensitive) == 0) { lists << i; break; }
    fillShelf(m_topLists, lists);
    m_profilePage->setListsVisible(!lists.isEmpty());
    m_profilePage->setSince(m_stats.since(), m_stats.empty());
}

void MainWindow::trackListening()
{
    // Only seconds actually heard: a seek or a pause adds nothing.
    const QString path = m_mpv.property("path").toString();
    const double now = m_mpv.property("time-pos").toDouble();
    const double step = now - m_listenLast;
    m_listenLast = now;
    if (path != m_listenPath || (step < -5 && now < 2)) {   // another song, or this one again
        m_listenPath = path;
        m_listenSeconds = 0;
        m_listenCounted = false;
        return;
    }
    if (m_mpv.property("pause").toBool() || step <= 0 || step > 2)
        return;
    m_listenSeconds += step;
    if (m_listenCounted || m_listenSeconds < 30 || path.isEmpty())
        return;
    m_listenCounted = true;
    const auto [artist, track] = playingSong();
    QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString());
    if (id.isEmpty())
        id = idForTarget(path);
    // On the video page it is a video; a song in the mini video is a song.
    const bool video = !m_mpv.property("user-data/ghost/audio").toBool() || (songVideo() && !panelVideo());
    if (!m_private)
        m_stats.played(track.isEmpty() ? m_mpv.property("media-title").toString() : track, artist, id, playingListPath(), video);
    if (m_pages->currentWidget() == m_profileScroll)
        m_profileSoon.start();
}

namespace {

// The YouTube menu's card: rounded, a little lighter than the window, with a
// soft shadow under it, so it seems to float beside the page. Painted once
// per change (no effect redrawing it off screen), so it costs nothing while
// the page scrolls or a game runs.
class FloatingCard : public QWidget {
public:
    using QWidget::QWidget;
    static constexpr int kShadow = 10;
    // Raised (2026-11-08 trial): its shadow is drawn by the layer under
    // the cards, the card fills its place and needs no hairline.
    bool raisedLook = false;
    QColor raisedFill;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (raisedLook) {
            QPainterPath card;
            card.addRoundedRect(QRectF(rect()).adjusted(0, 0, -(kShadow - 2), 0), 16, 16);
            p.fillPath(card, raisedFill);
            return;
        }
        // The card's top, bottom and left edges are the page card's and the
        // library's (Batın, 2026-11-08: they were 4 and 10 px off); the
        // shadow falls only to the right, towards the page.
        const QRectF body = QRectF(rect()).adjusted(0.5, 0.5, -(kShadow - 2) - 0.5, -0.5);
        for (int i = kShadow - 2; i > 0; --i) {
            QPainterPath ring;
            ring.addRoundedRect(body.adjusted(0, 2, i * 0.8, -2), 16, 16);
            p.fillPath(ring, QColor(0, 0, 0, 9));
        }
        QPainterPath card;
        card.addRoundedRect(body, 16, 16);
        p.fillPath(card, theme::c().cardAlt);   // a step lighter than a card, off the darker page
        p.setPen(QPen(theme::c().line, 1));
        p.drawPath(card);
    }
};

// The soft shadows under the raised cards, on the window's grey ground.
// A layer of its own under everything, painted only when the cards move or
// change size (no graphics effect redrawing a card on every scroll).
class ShadowLayer : public QWidget {
public:
    explicit ShadowLayer(QWidget *ground) : QWidget(ground)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        ground->installEventFilter(this);
        setGeometry(ground->rect());
    }
    std::function<QList<QRect>()> cards;   // in the ground's coordinates
    void follow(QWidget *card) { card->installEventFilter(this); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move || event->type() == QEvent::Show ||
            event->type() == QEvent::Hide) {
            if (watched == parent())
                setGeometry(parentWidget()->rect());
            update();
        }
        return false;
    }
    void paintEvent(QPaintEvent *) override
    {
        if (!cards)
            return;
        // On a dark ground a shadow cannot be seen as one: it only showed as
        // darker pixels round the cards' corners (Batin, 2026-11-12), so
        // there the cards sit flat on the ground.
        if (theme::ground().lightness() < 128)
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        constexpr int kSpread = 14;
        for (const QRect &r : cards()) {
            for (int i = kSpread; i > 0; --i) {
                QPainterPath ring;
                // Round on all four sides, a pixel lower than the card.
                ring.addRoundedRect(QRectF(r).adjusted(-i * 0.7, -i * 0.7 + 1, i * 0.7, i * 0.7 + 1), 14 + i * 0.7, 14 + i * 0.7);
                p.fillPath(ring, QColor(0, 0, 0, 7));
            }
        }
    }
};

} // namespace

static QString ytProfileSheet(bool compact);

QWidget *MainWindow::buildYouTubeSidebar()
{
    auto *panel = new FloatingCard;
    m_ytSidebar = panel;
    panel->installEventFilter(this);   // wide or icons only, by its width
    panel->setMinimumWidth(qRound(76 * theme::zoom()));
    theme::whenChanged(panel, [panel] { panel->update(); });
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(10, 12, FloatingCard::kShadow + 8, 12);
    box->setSpacing(2);

    const QString navStyle = QStringLiteral(
        "QPushButton { text-align: left; padding: 0 12px; min-height: 38px; border: none; border-radius: 10px;"
        " background: transparent; color: @text; font-size: 10.5pt; }"
        "QPushButton:hover { background: @raised; }"
        "QPushButton:checked { background: @raisedHover; color: @textBright; font-weight: 700; }");
    const auto sectionTitle = [&](const QString &text) {
        auto *label = new QLabel(text);
        theme::style(label, QStringLiteral("color: @dim; font-size: 8.5pt; font-weight: 600; background: transparent;"));
        label->setContentsMargins(12, 12, 0, 4);
        box->addWidget(label);
        m_ytWide << label;
        return label;
    };

    // The top: the button that folds the menu to icons, and the title.
    auto *fold = glyphButton(QChar(0xE700), "Collapse or expand the menu");
    connect(fold, &QPushButton::clicked, this, [this] {
        const bool opening = m_ytCompact;
        const int left = opening ? qRound(250 * theme::zoom()) : qRound(76 * theme::zoom());
        openLeftPanel(left, opening, [this](int got) {
            setUpdatesEnabled(false);
            setYtCompact(got < qRound(150 * theme::zoom()));
            setUpdatesEnabled(true);
        });
    });
    auto *title = new QLabel("YouTube");
    title->setObjectName("panelTitle");
    auto *top = new QHBoxLayout;
    top->setContentsMargins(2, 0, 0, 6);
    top->setSpacing(8);
    top->addWidget(fold);
    top->addWidget(title, 1);
    box->addLayout(top);
    m_ytWide << title;

    const auto nav = [&](QChar glyph, const QString &text) {
        auto *b = new QPushButton;
        b->setProperty("glyph", QString(glyph));
        b->setProperty("label", text);
        b->setText(QString(glyph) + QStringLiteral("    ") + text);
        b->setToolTip(text);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        theme::style(b, navStyle);
        QFont f = b->font();
        f.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Segoe Fluent Icons"), QStringLiteral("Segoe MDL2 Assets")});
        b->setFont(f);
        box->addWidget(b);
        return b;
    };
    QPushButton *search = nav(QChar(0xE721), "Search");
    m_ytSearchNav = search;
    // Lit while search results are open; a click opens the box and leaves
    // the row as the page has it.
    connect(search, &QPushButton::clicked, this, [this] {
        m_ytSearchNav->setChecked(m_ytMark == kYtSearch);
        openSearch();
    });
    QPushButton *home = nav(kIconHome, "Home");
    QPushButton *followingNav = nav(QChar(0xE734), "Following");
    m_ytNav << home;
    sectionTitle("Collection");
    QPushButton *history = nav(QChar(0xE81C), "History");
    QPushButton *later = nav(QChar(0xE823), "Watch later");
    m_ytNav << history << followingNav << later;   // indexes kept: 0 Home, 1 History, 2 Following, 3 Watch later
    connect(later, &QPushButton::clicked, this, &MainWindow::showWatchLater);
    connect(home, &QPushButton::clicked, this, &MainWindow::showYouTube);
    connect(history, &QPushButton::clicked, this, &MainWindow::showVideoHistory);
    connect(followingNav, &QPushButton::clicked, this, [this] { showFollowing(); });

    const QString listStyle = QStringLiteral(
        "QListWidget { background: transparent; }"
        "QListWidget::item { min-height: 34px; padding-left: 12px; border-radius: 8px; color: @text; }"
        "QListWidget::item:hover { background: @raised; }"
        "QListWidget::item:selected { background: @raisedHover; color: @textBright; }");
    sectionTitle("Your video lists");
    m_ytLists = new QListWidget;
    m_ytLists->setObjectName("songs");
    m_ytLists->setFrameShape(QFrame::NoFrame);
    m_ytLists->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_ytLists->setMouseTracking(true);
    theme::style(m_ytLists, listStyle);
    connect(m_ytLists, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        openPlaylist(item->data(Qt::UserRole).toInt());
    });
    box->addWidget(m_ytLists, 1);
    m_ytWide << m_ytLists;

    sectionTitle("Channels you follow");
    m_ytChannels = new QListWidget;
    m_ytChannels->setObjectName("songs");
    m_ytChannels->setFrameShape(QFrame::NoFrame);
    m_ytChannels->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_ytChannels->setMouseTracking(true);
    m_ytChannels->viewport()->installEventFilter(this);
    // No colour on the rows themselves, so a live channel's red shows.
    theme::style(m_ytChannels, QString(listStyle)
                                   .replace(QStringLiteral("QListWidget { background: transparent; }"),
                                            QStringLiteral("QListWidget { background: transparent; color: @text; }"))
                                   .replace(QStringLiteral("border-radius: 8px; color: @text; }"), QStringLiteral("border-radius: 8px; }")));
    connect(m_ytChannels, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString id = item->data(Qt::UserRole).toString();
        // On air now: the click watches the stream.
        if (m_liveNow.contains(id) && m_searchPage->onPlay) {
            const ytmusic::Item live = m_liveNow.value(id);
            m_searchPage->onPlay({live}, live.artist.isEmpty() ? item->data(kChannelNameRole).toString() : live.artist, 0, false, false);
            return;
        }
        if (!id.isEmpty()) {
            showYouTube();
            markYouTubeMenu(-1, -1, id);
            m_searchPage->showChannel(id, item->data(kChannelNameRole).toString());
        }
    });
    m_ytChannels->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_ytChannels, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_ytChannels->itemAt(pos);
        if (!item || item->data(Qt::UserRole).toString().isEmpty())
            return;
        const QString id = item->data(Qt::UserRole).toString(), name = item->data(kChannelNameRole).toString();
        QMenu menu(this);
        if (m_liveNow.contains(id))
            menu.addAction(QStringLiteral("Go to %1").arg(name), this, [this, id, name] {
                showYouTube();
                markYouTubeMenu(-1, -1, id);
                m_searchPage->showChannel(id, name);
            });
        menu.addAction(QStringLiteral("Unfollow %1").arg(name), this, [this, id, name] { setFollowing(id, name, false); });
        menu.exec(m_ytChannels->viewport()->mapToGlobal(pos));
    });
    box->addWidget(m_ytChannels, 1);
    // Folded, the followed channels stay as their pictures alone (Batın, 2026-11-10).

    // The bottom left: who is listening, Discord's face and name.
    m_ytProfile = new QPushButton;
    m_ytProfile->setCursor(Qt::PointingHandCursor);
    m_ytProfile->setIconSize(QSize(28, 28));
    theme::style(m_ytProfile, ytProfileSheet(false));
    connect(m_ytProfile, &QPushButton::clicked, this, &MainWindow::showProfile);
    // Folded to icons, the lists go and this keeps the icons at the top.
    auto *filler = new QWidget;
    filler->setObjectName("ytFiller");
    filler->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    filler->hide();
    box->addWidget(filler, 1);
    box->addSpacing(8);
    box->addWidget(m_ytProfile);
    return panel;
}

static QString ytProfileSheet(bool compact)
{
    return QStringLiteral(
               "QPushButton { text-align: %1; padding: 6px %2px; min-height: 40px; border: none; border-radius: 12px;"
               " background: @cardAlt; color: @textBright; font-weight: 600; }"
               "QPushButton:hover { background: @cardAltHover; }")
        .arg(compact ? QStringLiteral("center") : QStringLiteral("left"))
        .arg(compact ? 0 : 10);
}

void MainWindow::setYtCompact(bool compact)
{
    // Wide: icons with their names, the lists and the profile's name; narrow:
    // icons alone, centred, as the library's covers-only view.
    m_ytCompact = compact;
    for (QWidget *w : m_ytWide)
        w->setVisible(!compact);
    if (auto *filler = m_ytSidebar->findChild<QWidget *>("ytFiller"))
        filler->setVisible(false);
    // The followed channels: names when wide, pictures alone (centred) when folded.
    for (int i = 0; i < m_ytChannels->count(); ++i) {
        QListWidgetItem *row = m_ytChannels->item(i);
        const QString name = row->data(kChannelNameRole).toString();
        if (name.isEmpty())
            row->setHidden(compact);
        else
            row->setText(compact ? QString() : name);
    }
    padYtChannels();
    for (QPushButton *b : m_ytSidebar->findChildren<QPushButton *>()) {
        const QString glyph = b->property("glyph").toString();
        if (glyph.isEmpty())
            continue;
        b->setText(compact ? glyph : glyph + QStringLiteral("    ") + b->property("label").toString());
        b->setStyleSheet(b->styleSheet().replace(compact ? QStringLiteral("text-align: left") : QStringLiteral("text-align: center"),
                                                 compact ? QStringLiteral("text-align: center") : QStringLiteral("text-align: left")));
    }
    if (m_ytProfile) {
        m_ytProfile->setText(compact ? QString() : m_profileButton->toolTip());
        // The face alone sits in the middle (Batın, 2026-11-08: it was off to the left).
        theme::style(m_ytProfile, ytProfileSheet(compact));
    }
}

// The pictures of the folded menu sit in the middle of the list as it is now:
// worked out once, when the menu folded, the list could still be wide and
// the pictures stood off to the right, cut by the edge (Batın, 2026-11-11).
void MainWindow::padYtChannels()
{
    if (!m_ytChannels)
        return;
    const int left = m_ytCompact ? qMax(0, (m_ytChannels->viewport()->width() - 26) / 2 - 2) : 12;
    if (left == m_ytChannelPad)
        return;
    m_ytChannelPad = left;
    const QString sheet = m_ytChannels->property("ghostThemeSheet").toString();
    static const QRegularExpression pad(QStringLiteral(R"(padding-left: \d+px)"));
    theme::style(m_ytChannels, QString(sheet).replace(pad, QStringLiteral("padding-left: %1px").arg(left)));
}

void MainWindow::fillYouTubeSidebar()
{
    // The library's video lists (the song lists stay in the music layout).
    m_ytLists->clear();
    const auto &all = m_library.playlists();
    for (int i = 0; i < all.size(); ++i) {
        if (all[i].audio)
            continue;
        auto *row = new QListWidgetItem(all[i].name);
        row->setData(Qt::UserRole, i);
        row->setToolTip(QStringLiteral("%1 videos").arg(all[i].count));
        m_ytLists->addItem(row);
    }
    if (!m_ytLists->count()) {
        auto *row = new QListWidgetItem("No video lists yet");
        row->setFlags(Qt::NoItemFlags);
        m_ytLists->addItem(row);
    }
    m_ytChannels->clear();
    if (m_channelAvatars.isEmpty()) {
        QSettings saved;
        saved.beginGroup(QStringLiteral("youtube/avatars"));
        for (const QString &id : saved.childKeys())
            m_channelAvatars.insert(id, saved.value(id).toString());
        connect(&m_pictures, &RemotePictures::ready, this, [this](const QString &url) {
            for (const QString &known : std::as_const(m_channelAvatars))
                if (known == url) {
                    markLive();
                    return;
                }
        });
    }
    for (const auto &[id, name] : following()) {
        auto *row = new QListWidgetItem(m_ytCompact ? QString() : name);
        row->setData(Qt::UserRole, id);
        row->setData(kChannelNameRole, name);
        row->setToolTip("Click for its videos, right-click to unfollow");
        m_ytChannels->addItem(row);
    }
    if (!m_ytChannels->count()) {
        auto *row = new QListWidgetItem("None yet: Follow on a video's channel card");
        row->setFlags(Qt::NoItemFlags);
        row->setHidden(m_ytCompact);
        m_ytChannels->addItem(row);
    }
    markLive();
    // Who is on air: now, then every few minutes while the YouTube layout
    // is on screen (Batın, 2026-11-08: a live channel's name in red).
    if (!m_liveTimer) {
        m_liveTimer = new QTimer(this);
        m_liveTimer->setInterval(4 * 60 * 1000);
        connect(m_liveTimer, &QTimer::timeout, this, [this] {
            if (m_ytSidebar && m_ytSidebar->isVisible() && !isMinimized())
                checkLive();
        });
        m_liveTimer->start();
        checkLive();
    }
}

void MainWindow::checkLive()
{
    const auto channels = following();
    const int serial = ++m_liveSerial;
    auto found = std::make_shared<QHash<QString, ytmusic::Item>>();
    auto left = std::make_shared<int>(channels.size());
    if (channels.isEmpty()) {
        m_liveNow.clear();
        markLive();
        return;
    }
    for (const auto &[id, name] : channels) {
        m_ytClient.liveNow(id, [this, serial, found, left, id = id](const ytmusic::Item &live) {
            if (serial != m_liveSerial)
                return;
            const QString avatar = live.avatar.startsWith(QLatin1String("//")) ? QStringLiteral("https:") + live.avatar : live.avatar;
            if (!avatar.isEmpty() && m_channelAvatars.value(id) != avatar) {
                m_channelAvatars.insert(id, avatar);
                QSettings().setValue(QStringLiteral("youtube/avatars/") + id, avatar);
            }
            if (!live.id.isEmpty())
                found->insert(id, live);
            if (--*left == 0) {
                m_liveNow = *found;
                markLive();
            }
        });
    }
}

QPixmap MainWindow::channelFace(const QString &id, bool live, int shown)
{
    // A channel's own picture, round; one on air has a red ring and a small
    // LIVE under it, as YouTube draws it (Batın, 2026-11-10). Drawn at 26 and
    // scaled to the size asked for.
    const qreal dpr = 2;
    const int side = 26;
    QPixmap pm(QSize(shown, shown) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.scale(qreal(shown) / side, qreal(shown) / side);
        const QRectF circle = live ? QRectF(3, 2, side - 6, side - 6) : QRectF(1, 1, side - 2, side - 2);
        QPainterPath round;
        round.addEllipse(circle);
        const QPixmap picture = m_pictures.get(m_channelAvatars.value(id), id);   // kept on disk too
        if (picture.isNull()) {
            p.fillPath(round, theme::c().raised);
        } else {
            p.save();
            p.setClipPath(round);
            p.drawPixmap(circle, picture, QRectF(picture.rect()));
            p.restore();
        }
        if (live) {
            const QColor red(0xE5, 0x2B, 0x2B);
            p.setPen(QPen(red, 1.6));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(circle.adjusted(-1.5, -1.5, 1.5, 1.5));
            const QRectF tag(4, side - 9, side - 8, 9);
            p.setPen(Qt::NoPen);
            p.setBrush(red);
            p.drawRoundedRect(tag, 2, 2);
            QFont f(QStringLiteral("Segoe UI"));
            f.setPixelSize(7);
            f.setBold(true);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(tag, Qt::AlignCenter, QStringLiteral("LIVE"));
        }
    }
    return pm;
}

void MainWindow::showVideoChannel()
{
    // The channel's picture beside its name under the video, with LIVE when
    // the stream is on air (Batın, 2026-11-12). A channel not followed has
    // no picture kept: asked once, as the live check asks for followed ones.
    const QString id = m_videoChannelId;
    if (id.isEmpty()) {
        m_videoPage->setChannelFace(QPixmap());
        return;
    }
    if (!m_channelAvatars.contains(id) && !m_avatarAsked.contains(id)) {
        m_avatarAsked.insert(id);
        m_ytClient.liveNow(id, [this, id](const ytmusic::Item &live) {
            const QString avatar = live.avatar.startsWith(QLatin1String("//")) ? QStringLiteral("https:") + live.avatar : live.avatar;
            if (avatar.isEmpty())
                return;
            m_channelAvatars.insert(id, avatar);
            if (id == m_videoChannelId)
                showVideoChannel();
        });
    }
    const bool live = m_mpv.property("user-data/ghost/live").toBool();
    m_videoPage->setChannelFace(channelFace(id, live, 36));
}

void MainWindow::markLive()
{
    const int side = 26;
    const auto face = [&](const QString &id, bool live) { return QIcon(channelFace(id, live, side)); };
    m_ytChannels->setIconSize(QSize(side, side));
    for (int i = 0; i < m_ytChannels->count(); ++i) {
        QListWidgetItem *row = m_ytChannels->item(i);
        const QString id = row->data(Qt::UserRole).toString();
        if (id.isEmpty())
            continue;
        const bool live = m_liveNow.contains(id);
        row->setIcon(face(id, live));
        if (live) {
            row->setForeground(QColor(0xFF, 0x4D, 0x4D));
            row->setToolTip(QStringLiteral("%1 · live now: %2\nClick to watch, right-click for the channel")
                                .arg(row->data(kChannelNameRole).toString(), m_liveNow.value(id).title));
        } else {
            row->setData(Qt::ForegroundRole, QVariant());
            row->setToolTip(row->data(kChannelNameRole).toString() + QStringLiteral("\nClick for its videos, right-click to unfollow"));
        }
    }
}

QList<QPair<QString, QString>> MainWindow::following() const
{
    QList<QPair<QString, QString>> out;
    for (const QString &line : QSettings().value("youtube/following").toStringList()) {
        const int tab = line.indexOf('\t');
        if (tab > 0)
            out.append({line.left(tab), line.mid(tab + 1)});
    }
    return out;
}

bool MainWindow::follows(const QString &channelId) const
{
    for (const auto &[id, name] : following())
        if (id == channelId)
            return true;
    return false;
}

void MainWindow::setFollowing(const QString &channelId, const QString &name, bool on)
{
    if (channelId.isEmpty())
        return;
    QStringList lines;
    for (const auto &[id, n] : following())
        if (id != channelId)
            lines << id + '\t' + n;
    if (on)
        lines.prepend(channelId + '\t' + (name.isEmpty() ? channelId : name));
    QSettings().setValue("youtube/following", lines);
    toast(on ? QStringLiteral("Following %1").arg(name) : QStringLiteral("Unfollowed %1").arg(name));
    m_liveNow.remove(channelId);
    fillYouTubeSidebar();
    updateFollowButton();
    // A channel just followed may be on air now: ask about it at once.
    if (on)
        m_ytClient.liveNow(channelId, [this, channelId](const ytmusic::Item &live) {
            bool still = false;
            for (const auto &[id, n] : following())
                still = still || id == channelId;
            if (!live.id.isEmpty() && still) {
                m_liveNow.insert(channelId, live);
                markLive();
            }
        });
}

void MainWindow::showFollowing(const QString &onlyChannel)
{
    showYouTube();
    if (onlyChannel.isEmpty())
        markYouTubeMenu(2);
    else
        markYouTubeMenu(-1, -1, onlyChannel);
    QList<QPair<QString, QString>> channels;
    QString title = QStringLiteral("Following");
    for (const auto &c : following())
        if (onlyChannel.isEmpty() || c.first == onlyChannel) {
            channels << c;
            if (!onlyChannel.isEmpty())
                title = c.second;
        }
    if (channels.isEmpty()) {
        m_searchPage->showVideos("Following: no channels yet. Play a video, then press Follow on its channel card "
                                 "(right panel), or right-click a video.", {});
        return;
    }
    m_searchPage->showVideos(title + QStringLiteral(": loading..."), {});
    // Every channel's feed, then all of them together, newest first.
    const int serial = ++m_followSerial;
    auto all = std::make_shared<QVector<ytmusic::Item>>();
    auto waiting = std::make_shared<int>(channels.size());
    // Each channel's own Videos tab (lengths, pictures, no Shorts); its
    // public feed if that fails. Its ages ("3 days ago") give the order.
    const auto finish = [this, serial, all, waiting, title] {
        if (serial != m_followSerial || --*waiting > 0)
            return;
        std::stable_sort(all->begin(), all->end(), [](const ytmusic::Item &a, const ytmusic::Item &b) { return a.published > b.published; });
        m_searchPage->showVideos(title, *all);
    };
    for (const auto &[id, name] : channels) {
        const QString channelId = id, channelName = name;
        m_ytClient.channelVideos(channelId, {}, [this, serial, all, finish, channelId, channelName](ytmusic::ChannelPage page) {
            if (serial != m_followSerial)
                return;
            if (page.videos.isEmpty()) {
                m_ytClient.channelFeed(channelId, [this, serial, all, finish](QVector<ytmusic::Item> items) {
                    if (serial != m_followSerial)
                        return;
                    const qint64 now = QDateTime::currentSecsSinceEpoch();
                    for (ytmusic::Item &it : items) {
                        const qint64 ago = now - it.published;
                        const QString when = ago < 3600 ? QStringLiteral("%1 minutes ago").arg(qMax<qint64>(1, ago / 60))
                                           : ago < 86400 ? QStringLiteral("%1 hours ago").arg(ago / 3600)
                                           : ago < 86400 * 30 ? QStringLiteral("%1 days ago").arg(ago / 86400)
                                           : ago < 86400 * 365 ? QStringLiteral("%1 months ago").arg(ago / (86400 * 30))
                                                               : QStringLiteral("%1 years ago").arg(ago / (86400 * 365));
                        const qint64 views = it.extra.toLongLong();
                        const QString count = views >= 1000000 ? QStringLiteral("%1M views").arg(views / 1000000)
                                            : views >= 1000 ? QStringLiteral("%1K views").arg(views / 1000)
                                                            : QStringLiteral("%1 views").arg(views);
                        it.extra = count + QStringLiteral("  •  ") + when;
                    }
                    *all += items;
                    finish();
                });
                return;
            }
            // "5 hours ago", "Streamed 2 weeks ago": about when, for the order.
            static const QRegularExpression age(QStringLiteral("(\\d+)\\s+(second|minute|hour|day|week|month|year)s?\\s+ago"));
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            for (ytmusic::Item &it : page.videos) {
                it.artist = page.name.isEmpty() ? channelName : page.name;
                if (it.avatar.isEmpty())
                    it.avatar = page.avatar;
                const QRegularExpressionMatch m = age.match(it.extra);
                if (m.hasMatch()) {
                    static const QHash<QString, qint64> unit{{"second", 1}, {"minute", 60}, {"hour", 3600}, {"day", 86400},
                                                             {"week", 7 * 86400}, {"month", 30 * 86400}, {"year", 365 * 86400}};
                    it.published = now - m.captured(1).toLongLong() * unit.value(m.captured(2));
                }
            }
            *all += page.videos;
            finish();
        });
    }
}

void MainWindow::updateFollowButton()
{
    if (!m_videoChannelId.isEmpty())
        m_videoPage->setChannel(m_videoChannelId, follows(m_videoChannelId));
    m_panel->setFollow(!m_panelChannelId.isEmpty(), follows(m_panelChannelId));
    m_searchPage->refreshFollow();
}

QVector<ytmusic::Item> MainWindow::watchLater() const
{
    QVector<ytmusic::Item> out;
    const QJsonArray saved = QJsonDocument::fromJson(QSettings().value("youtube/watchLater").toByteArray()).array();
    for (const QJsonValue &v : saved) {
        const QJsonObject o = v.toObject();
        ytmusic::Item it;
        it.kind = ytmusic::Kind::Video;
        it.id = o.value("id").toString();
        it.title = o.value("title").toString();
        it.artist = o.value("channel").toString();
        it.channelId = o.value("channelId").toString();
        it.avatar = o.value("avatar").toString();
        it.duration = o.value("length").toString();
        if (!it.id.isEmpty())
            out << it;
    }
    return out;
}

void MainWindow::setWatchLater(const QVector<ytmusic::Item> &items, bool keep)
{
    QVector<ytmusic::Item> list = watchLater();
    QSet<QString> ids;
    for (const ytmusic::Item &it : items)
        ids.insert(it.id);
    list.erase(std::remove_if(list.begin(), list.end(), [&ids](const ytmusic::Item &it) { return ids.contains(it.id); }), list.end());
    if (keep)
        list = items + list;   // newest first
    QJsonArray saved;
    for (const ytmusic::Item &it : list)
        saved.append(QJsonObject{{"id", it.id}, {"title", it.title}, {"channel", it.artist}, {"channelId", it.channelId},
                                 {"avatar", it.avatar}, {"length", it.duration}});
    QSettings().setValue("youtube/watchLater", QJsonDocument(saved).toJson(QJsonDocument::Compact));
    toast(keep ? (items.size() == 1 ? QStringLiteral("Saved to Watch later") : QStringLiteral("%1 videos saved to Watch later").arg(items.size()))
               : QStringLiteral("Removed from Watch later"));
    if (m_ytNav.size() > 3 && m_ytNav[3]->isChecked())
        showWatchLater();
}

void MainWindow::showWatchLater()
{
    showYouTube();
    markYouTubeMenu(3);
    const QVector<ytmusic::Item> items = watchLater();
    m_searchPage->showVideos(items.isEmpty() ? QStringLiteral("Watch later: nothing yet. Right-click a video, then Save to Watch later.")
                                             : QStringLiteral("Watch later"), items);
}

void MainWindow::showVideoHistory()
{
    showYouTube();
    markYouTubeMenu(1);
    QVector<ytmusic::Item> items;
    QSet<QString> seen;
    const QDateTime now = QDateTime::currentDateTime();
    for (const ListeningStats::Entry &e : m_stats.history()) {
        if (!e.video || e.id.isEmpty() || seen.contains(e.id))
            continue;
        seen.insert(e.id);
        ytmusic::Item it;
        it.kind = ytmusic::Kind::Video;
        it.id = e.id;
        it.title = e.title;
        it.artist = e.artist;
        const qint64 days = e.at.daysTo(now);
        it.extra = days <= 0 ? QStringLiteral("watched today") : days == 1 ? QStringLiteral("watched yesterday")
                                                                           : QStringLiteral("watched %1 days ago").arg(days);
        items << it;
    }
    m_searchPage->showVideos("History", items);
}

namespace {

// A widget as it is on screen with mpv's picture in it (a widget's own grab
// sees that window black), and only this app: Windows draws the window for
// us (PrintWindow, full content), so a window lying over it (Discord, a
// browser) is not in the picture as it was with a screen grab.
QPixmap grabWithVideo(QWidget *widget, QRect part = {})
{
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
    QWidget *top = widget->window();
    const HWND hwnd = reinterpret_cast<HWND>(top->winId());
    RECT r;
    if (!GetWindowRect(hwnd, &r))
        return {};
    const int w = r.right - r.left, h = r.bottom - r.top;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(dc, bitmap);
    const bool drawn = PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT);
    SelectObject(dc, old);
    const QImage whole = drawn ? QImage::fromHBITMAP(bitmap) : QImage();
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    if (whole.isNull())
        return {};
    const qreal dpr = top->devicePixelRatioF();
    if (part.isNull())
        part = widget->rect();   // all of it, or `part` of it in its own coordinates
    const QPoint at = (widget->mapToGlobal(part.topLeft()) * dpr) - QPoint(r.left, r.top);
    QPixmap out = QPixmap::fromImage(whole.copy(QRect(at, part.size() * dpr)));
    out.setDevicePixelRatio(dpr);
    return out;
}

// The app's ghost (assets/make_icon.py's shape, on its 256 grid) as a path:
// drawn, not a picture, so it is sharp at any size and takes the accent.
QPainterPath ghostPath(qreal shift)
{
    static const QPointF body[] = {{56, 226}, {56, 96}, {104, 40}, {160, 40}, {212, 92}, {212, 196}, {198, 232},
                                   {176, 206}, {154, 236}, {132, 208}, {110, 236}, {88, 208}};
    static const QPointF eyes[2][4] = {{{90, 106}, {120, 96}, {120, 138}, {90, 148}},
                                       {{140, 96}, {170, 106}, {170, 148}, {140, 138}}};
    QPainterPath path;
    QPolygonF outline;
    for (const QPointF &p : body)
        outline << p + QPointF(shift, shift);
    path.addPolygon(outline);
    path.closeSubpath();
    for (const auto &eye : eyes) {
        QPolygonF hole;
        for (const QPointF &p : eye)
            hole << p + QPointF(shift, shift);
        QPainterPath cut;
        cut.addPolygon(hole);
        cut.closeSubpath();
        path = path.subtracted(cut);
    }
    return path;
}

// The switch between the two layouts as a short intro, the way a game or a
// TV app opens on its mark (Batın, 2026-11-08): the ghost comes up over the
// old page, floats a moment with the layout's name, and grows away to show
// the new one. ~0.75 s; the pages under it are pictures taken before.
class GhostIntro : public QWidget
{
public:
    GhostIntro(QWidget *parent, const QPixmap &before, const QPixmap &after, const QString &name)
        : QWidget(parent), m_before(before), m_after(after), m_name(name)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *run = new QVariantAnimation(this);
        run->setDuration(760);
        run->setStartValue(0.0);
        run->setEndValue(1.0);
        connect(run, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            m_t = v.toReal();
            update();
        });
        connect(run, &QAbstractAnimation::finished, this, &QObject::deleteLater);
        run->start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.drawPixmap(0, 0, m_t < 0.45 ? m_before : m_after);
        // The veil: up fast, held, away at the end.
        const qreal veil = m_t < 0.22 ? m_t / 0.22 : m_t > 0.68 ? qMax(0.0, 1 - (m_t - 0.68) / 0.32) : 1.0;
        QColor back = theme::window();
        back.setAlphaF(veil);
        p.fillRect(rect(), back);
        // The ghost: pops in (a little overshoot), floats, grows away.
        qreal scale, alpha;
        if (m_t < 0.4) {
            const qreal k = m_t / 0.4;
            scale = 0.55 + 0.45 * QEasingCurve(QEasingCurve::OutBack).valueForProgress(k);
            alpha = qMin(1.0, k * 1.6);
        } else if (m_t < 0.66) {
            scale = 1.0;
            alpha = 1.0;
        } else {
            const qreal k = (m_t - 0.66) / 0.34;
            scale = 1.0 + 0.7 * QEasingCurve(QEasingCurve::InCubic).valueForProgress(k);
            alpha = 1 - k;
        }
        const qreal side = qMin(width(), height()) * 0.22;
        const qreal floatY = std::sin(m_t * 3.14159265358979 * 2) * side * 0.04;
        const QPointF centre(width() / 2.0, height() / 2.0 - side * 0.18 + floatY);
        p.save();
        p.setOpacity(alpha);
        // A soft glow of the accent behind it.
        QColor glow = theme::accent();
        QRadialGradient halo(centre, side * scale);
        glow.setAlphaF(0.28);
        halo.setColorAt(0, glow);
        glow.setAlphaF(0);
        halo.setColorAt(1, glow);
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(centre, side * scale, side * scale);
        p.translate(centre);
        p.rotate(std::sin(m_t * 3.14159265358979 * 3) * 4 * (1 - m_t));   // a small sway as it moves
        p.scale(side * scale / 256.0, side * scale / 256.0);
        p.translate(-134, -138);
        p.setBrush(QColor(42, 46, 48));
        p.drawPath(ghostPath(-8));
        p.setBrush(theme::accent());
        p.drawPath(ghostPath(0));
        p.restore();
        // The layout's name under it, in only while the ghost holds.
        const qreal word = m_t < 0.25 ? 0 : m_t < 0.4 ? (m_t - 0.25) / 0.15 : m_t < 0.62 ? 1 : qMax(0.0, 1 - (m_t - 0.62) / 0.12);
        if (word > 0) {
            p.setOpacity(word);
            QFont f = font();
            f.setPixelSize(qMax(14, int(side * 0.16)));
            f.setWeight(QFont::DemiBold);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
            p.setFont(f);
            p.setPen(theme::c().textBright);
            p.drawText(QRectF(0, centre.y() + side * 0.62, width(), side * 0.4), Qt::AlignHCenter | Qt::AlignTop, m_name);
        }
    }

private:
    QPixmap m_before, m_after;
    QString m_name;
    qreal m_t = 0;
};

} // namespace

void MainWindow::applyLayoutColors()
{
    // Batın, 2026-11-08: a grey ground under the whole window and the cards
    // a step lighter on it, each with a soft shadow, as if they stood off it
    // (Black has no room for a shadow, so the ground goes grey). First the
    // YouTube layout, then the music layout too, so the two read as one app.
    if (!m_mainPanel || !centralWidget())
        return;
    auto *menu = static_cast<FloatingCard *>(m_ytSidebar);
    const QColor ground = theme::ground();   // Black 18, Light ~240
    const QColor page = theme::panel();      // Black 24, Light 255
    const QColor raised = theme::panelRaised();
    // The library, the page and the now playing panel are all "#panel"-like
    // cards on the ground; the YouTube menu is a step lighter still.
    centralWidget()->setStyleSheet(QStringLiteral(
        "QWidget#central, QWidget#topBar, QWidget#nowPlaying { background: %1; }"
        "QSplitter::handle { background: transparent; }"
        "QWidget#libraryPanel, QWidget#panel { background: %2; border-radius: 12px; }")
                                       .arg(ground.name(), page.name()));
    // On Light the cards' shadow shows on all four sides (Batin, 2026-11-12:
    // it fell only into the gaps between them, the top and bottom edges met
    // the strip and the bar flat), so there the cards stand 8 px off them as
    // they do off each other. The dark themes have no shadow and keep the
    // cards against the strip and the bar.
    if (m_bodyBox) {
        const int edge = theme::c().light ? 8 : 0;
        // The YouTube side has no song bar under the cards: they stand off
        // the window's bottom edge as off each other, floating (Batin,
        // 2026-11-20: flush with the edge they looked cut off).
        m_bodyBox->setContentsMargins(8, edge, 8, m_youtubeLayout ? 8 : edge);
    }
    if (menu) {
        menu->raisedLook = true;
        menu->raisedFill = raised;
        menu->update();
    }
    if (!m_shadows) {
        auto *layer = new ShadowLayer(centralWidget());
        layer->cards = [this] {
            QList<QRect> out;
            QWidget *ground = centralWidget();
            const auto add = [&](QWidget *w, int trimRight = 0) {
                if (w && w->isVisible() && w->width() > 0)
                    out << QRect(w->mapTo(ground, QPoint(0, 0)), w->size() - QSize(trimRight, 0));
            };
            add(m_ytSidebar, FloatingCard::kShadow - 2);
            add(m_libraryPanel);
            add(m_mainPanel);
            add(m_panel);
            return out;
        };
        for (QWidget *w : {static_cast<QWidget *>(m_mainPanel), m_ytSidebar, m_libraryPanel, static_cast<QWidget *>(m_panel)})
            if (w)
                static_cast<ShadowLayer *>(layer)->follow(w);
        m_shadows = layer;
    }
    m_shadows->show();
    m_shadows->lower();
    m_shadows->update();
}
void MainWindow::holdSurfaceWhile(QObject *animation)
{
    // mpv's picture is a window over the app that no widget can lie on
    // (it is put back on top whenever the page moves), so while the switch
    // plays it is put away, the sound going on, and comes back after.
    m_surfaceHeld = true;
    placeSurface();
    connect(animation, &QObject::destroyed, this, [this] {
        m_surfaceHeld = false;
        placeSurface();
    });
}

void MainWindow::applyUsage()
{
    // Music only / YouTube only (Settings > Appearance): the video button is
    // gone and the app stays on that side (Batın, 2026-11-12).
    const QString usage = QSettings().value("look/usage", "both").toString();
    m_youtubeButton->setVisible(usage == QLatin1String("both"));
    if (usage == QLatin1String("music") && m_youtubeLayout)
        setYouTubeLayout(false, false);
    else if (usage == QLatin1String("youtube") && !m_youtubeLayout)
        setYouTubeLayout(true, false);
}

void MainWindow::setYouTubeLayout(bool youtube, bool animate, const std::function<void()> &then)
{
    {
        const QString usage = QSettings().value("look/usage", "both").toString();
        if (usage == QLatin1String("music"))
            youtube = false;
        else if (usage == QLatin1String("youtube"))
            youtube = true;
        // Held on its side already: only what was to follow.
        if (usage != QLatin1String("both") && youtube == m_youtubeLayout) {
            if (then)
                then();
            return;
        }
    }
    // How the layouts change: the ghost intro (default), a slide, or at once.
    const QString style = QSettings().value("look/layoutSwitch", "ghost").toString();
    const bool slideStyle = style == QLatin1String("slide");
    // The page as it is, to slide away; not over a video, whose picture is a
    // window of its own the slide could not carry.
    // Over a video (its picture is mpv's own window, which a widget's grab
    // sees black) the page is taken from the screen, and the cover is made a
    // window of its own so it can lie over mpv's (Batın, 2026-11-08: the
    // intro did not show while a video was open).
    // What changes runs from the cards' top to the window's bottom: the song
    // bar shows on one side only, so the cards are taller on the other. Only
    // the cards were taken before, and the strip where the bar had been
    // showed the new side through the old one (Batin, 2026-11-20).
    const auto changing = [this] {
        const QPoint at = m_split->mapTo(this, QPoint(0, 0));
        const QWidget *ground = centralWidget();
        return QRect(ground->mapTo(this, QPoint(0, 0)).x(), at.y(), ground->width(),
                     ground->mapTo(this, QPoint(0, ground->height())).y() - at.y());
    };
    const QRect area = changing();
    QPixmap before;
    const bool overVideo = videoShowing();
    if (animate && style != QLatin1String("instant") && isVisible() && !isMinimized()) {
        if (overVideo) {
            before = grabWithVideo(this, area);
            if (before.isNull())
                before = grab(area);
        } else {
            before = grab(area);
        }
    }
    // Each layout keeps its own left width: the library's covers and the
    // YouTube menu are folded or opened apart.
    if (animate && youtube != m_youtubeLayout)
        QSettings().setValue(m_youtubeLayout ? "layout/ytWidth" : "layout/libraryWidth", m_split->sizes().value(0));
    m_youtubeLayout = youtube;
    QSettings().setValue("layout/youtube", youtube);
    if (m_nowPlaying && m_pages && m_videoPage)
        m_nowPlaying->setVisible(!youtube && !m_videoFull && !(m_pages->currentWidget() == m_videoPage && !m_videoPoppedOut));
    updatePanelVisibility();
    // The splitter gives the now playing view's room back on its next turn;
    // the switch takes its picture of the new side before that, and the
    // view's place showed empty through the intro (Batin, 2026-11-20).
    if (m_rightSplit)
        m_rightSplit->refresh();
    tabsChanged();   // the tabs are the YouTube side's
    // The YouTube menu has the profile at its foot: the top one only on the
    // music side (Batin, 2026-11-20).
    if (m_profileButton)
        m_profileButton->setVisible(!youtube);
    SearchPopup::setYouTube(youtube);
    {
        const int wanted = QSettings().value(youtube ? "layout/ytWidth" : "layout/libraryWidth", -1).toInt();
        const int total = m_split->sizes().value(0) + m_split->sizes().value(1);
        if (animate && wanted > 0 && total > wanted)
            m_split->setSizes({wanted, total - wanted});
    }
    theme::style(m_youtubeButton, youtube ? QStringLiteral("color: @accentText;") : QString());
    m_leftStack->setCurrentIndex(youtube ? 1 : 0);
    applyLayoutColors();
    if (youtube) {
        fillYouTubeSidebar();
        showYouTube();
    } else {
        // What was typed for YouTube is not a music search (2026-11-12).
        {
            const QSignalBlocker quiet(m_search);
            m_search->clear();
        }
        m_searchTimer.stop();
        m_search->setPlaceholderText("What do you want to play?");
        if (m_searchPage->youTubeMode())
            m_searchPage->setYouTubeMode(false);
        showHome();
    }
    if (then)
        then();
    if (before.isNull())
        return;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    const QPixmap after = grab(area);
    if (!slideStyle) {
        // A child of the window itself, as mpv's picture is: only a sibling
        // can be raised over it.
        auto *intro = new GhostIntro(this, before, after, youtube ? "YouTube" : "Music");
        if (overVideo)
            holdSurfaceWhile(intro);
        intro->setGeometry(area);
        intro->show();
        intro->raise();
        return;
    }
    auto *cover = new QWidget(this);
    if (overVideo)
        holdSurfaceWhile(cover);
    cover->setGeometry(area);
    cover->setAttribute(Qt::WA_TransparentForMouseEvents);
    const int w = area.width();
    const int dir = youtube ? 1 : -1;   // YouTube comes in from the right, music from the left
    auto *old = new QLabel(cover);
    old->setPixmap(before);
    old->setGeometry(0, 0, w, area.height());
    auto *fresh = new QLabel(cover);
    fresh->setPixmap(after);
    fresh->setGeometry(dir * w, 0, w, area.height());
    cover->show();
    cover->raise();
    auto *group = new QParallelAnimationGroup(cover);
    for (auto [label, from, to] : {std::tuple{old, 0, -dir * w}, std::tuple{fresh, dir * w, 0}}) {
        auto *slide = new QPropertyAnimation(label, "pos", group);
        slide->setDuration(240);
        slide->setEasingCurve(QEasingCurve::OutCubic);
        slide->setStartValue(QPoint(from, 0));
        slide->setEndValue(QPoint(to, 0));
        group->addAnimation(slide);
    }
    connect(group, &QAbstractAnimation::finished, cover, &QObject::deleteLater);
    group->start();
}

void MainWindow::markYouTubeMenu(int nav, int list, const QString &channel)
{
    m_ytMark = nav;
    for (int i = 0; i < m_ytNav.size(); ++i)
        m_ytNav[i]->setChecked(i == nav);
    if (m_ytSearchNav)
        m_ytSearchNav->setChecked(nav == kYtSearch);
    const auto mark = [](QListWidget *rows, const QVariant &key) {
        if (!rows)
            return;
        QListWidgetItem *hit = nullptr;
        for (int i = 0; i < rows->count() && key.isValid() && !hit; ++i)
            if (rows->item(i)->data(Qt::UserRole) == key)
                hit = rows->item(i);
        if (hit) {
            rows->setCurrentItem(hit);
        } else {
            rows->clearSelection();
            rows->setCurrentItem(nullptr);
        }
    };
    mark(m_ytLists, list >= 0 ? QVariant(list) : QVariant());
    mark(m_ytChannels, channel.isEmpty() ? QVariant() : QVariant(channel));
}

void MainWindow::showYouTube()
{
    markYouTubeMenu(0);
    QWidget *now = m_pages->currentWidget();
    if (now != m_searchPage)
        m_beforeSearch = now == m_videoPage || now == m_lyricsView ? m_pages->widget(0) : now;
    if (now == m_lyricsView)
        m_beforeLyrics = nullptr;
    {
        const QSignalBlocker quiet(m_search);
        m_search->clear();
    }
    m_searchTimer.stop();
    m_searchPage->setYouTubeMode(true);
    m_searchPage->setQuery(QString());
    m_searchPage->showYouTubeHome();
    m_search->setPlaceholderText("Search YouTube");
    m_pages->setCurrentWidget(m_searchPage);
}

void MainWindow::showHome()
{
    if (m_youtubeLayout) {
        showYouTube();
        return;
    }
    // The music home: the box searches music again.
    if (m_searchPage->youTubeMode())
        m_searchPage->setYouTubeMode(false);
    m_search->setPlaceholderText("What do you want to play?");
    m_open = -1;
    m_libraryList->clearSelection();
    m_pages->setCurrentIndex(0);
}

void MainWindow::openPlaylist(int index, bool keepView)
{
    const auto &all = m_library.playlists();
    if (index < 0 || index >= all.size())
        return;
    // Reopened after its file changed (a drag, a removal, a rescan): stay
    // where the list was scrolled and keep the search.
    const int scrolled = keepView ? m_songs->verticalScrollBar()->value() : 0;
    const QString searched = keepView ? m_songSearch->text() : QString();
    m_open = index;
    // The library shows which list is open, however it was opened (a row,
    // the island's or the bar's cover, the profile): Batin, 2026-11-12, the
    // cover went to Liked songs and the row stayed on the list before.
    {
        QListWidgetItem *mark = nullptr;
        for (int r = 0; r < m_libraryList->count() && !mark; ++r) {
            QListWidgetItem *row = m_libraryList->item(r);
            if (row->data(tiles::kFolderRole).toString().isEmpty() && row->data(tiles::kIndexRole).toInt() == index &&
                row->data(tiles::kIndexRole).isValid())
                mark = row;
        }
        if (mark) {
            if (m_libraryList->currentItem() != mark)
                m_libraryList->setCurrentItem(mark);
        } else {
            m_libraryList->clearSelection();
            m_libraryList->setCurrentItem(nullptr);
        }
    }
    markYouTubeMenu(-1, index);   // a video list's row on the YouTube menu
    const Playlist &p = all[index];
    m_playlistKind->setText(p.audio ? "PLAYLIST  ·  SONGS" : "PLAYLIST  ·  VIDEOS");
    m_playlistInfo->setText(subtitleFor(p) + QStringLiteral("  ·  added ") +
                            QLocale().toString(p.modified.date(), QLocale::ShortFormat));
    showSharedBy(p.shared);
    showDescription();
    static_cast<PlaylistHeader *>(m_playlistCover)->setName(p.name);
    static_cast<PlaylistHeader *>(m_playlistCover)->setTint(
        QSettings().value("headerColor/" + QFileInfo(p.path).fileName()).value<QColor>());

    setVideoGrid(!p.audio);
    // Recommendations for another list are asked again; the same list
    // (reopened after a change) keeps the ones shown, less any added.
    if (!p.audio) {
        m_recBox->hide();
        updateRecCard();
        m_recFor.clear();
    } else if (p.path != m_recFor) {
        m_recFor = p.path;
        m_recAutoArmed = true;
        setRecommendedOpen(false);
        QTimer::singleShot(0, this, [this] { loadRecommended(true); });
    } else {
        QTimer::singleShot(0, this, [this] { showRecommended(false); });
    }
    m_songSearch->blockSignals(true);
    m_songSearch->setText(searched);
    m_songSearch->blockSignals(false);
    m_songs->clear();
    const QString added = relativeTime(p.modified);
    int entryIndex = 0;
    for (const PlaylistEntry &entry : p.readEntries()) {
        const auto [track, artist] = splitTitle(entry.title);
        auto *row = new QListWidgetItem(track);
        row->setData(Qt::UserRole, entry.target);
        row->setData(tiles::kEntryRole, entryIndex++);
        row->setData(tiles::kArtistRole, artist);
        row->setData(tiles::kAddedRole, entry.added.isValid() ? relativeTime(entry.added) : added);
        row->setData(tiles::kAddedAtRole, entry.added.isValid() ? entry.added : p.modified);
        row->setData(tiles::kOfflineRole, m_offline.has(entry.target));
        m_songs->addItem(row);
    }
    // Each list keeps the sort it was last shown with, as in Spotify.
    const QStringList sort = QSettings().value("sort/" + QFileInfo(p.path).fileName()).toStringList();
    m_sortKey = static_cast<SortKey>(sort.value(0).toInt());
    m_shuffleSeed = sort.value(2).toUInt();
    m_sortDescending = sort.value(1) == "desc";
    fillDurations();
    applySort();   // and filterSongs, which shows the empty hint
    showDownloadState();
    m_pages->setCurrentIndex(1);
    m_visitSoon.start();   // another list on the same page is another place
    if (keepView) {
        m_songs->verticalScrollBar()->setValue(scrolled);
    } else {
        m_songs->scrollToTop();
        m_playlistCover->setFixedHeight(headerTallNow());
    }
    layoutHeader();
    // Ask for every song's cover now rather than as each row scrolls into
    // view, so the list fills in while it is being looked at.
    for (int row = 0; row < m_songs->count(); ++row)
        m_covers.get(idForTarget(m_songs->item(row)->data(Qt::UserRole).toString()), ThumbnailCache::Size::Card);
}

// -- songs: sort, search, reorder ---------------------------------------------

void MainWindow::sortBy(SortKey key)
{
    // Up, down, then back to the list's own order.
    if (key == SortKey::Custom || key != m_sortKey) {
        m_sortKey = key;
        m_sortDescending = false;
    } else if (!m_sortDescending) {
        m_sortDescending = true;
    } else {
        m_sortKey = SortKey::Custom;
        m_sortDescending = false;
    }
    if (m_open >= 0) {
        const QString file = QFileInfo(m_library.playlists()[m_open].path).fileName();
        QSettings settings;
        if (m_sortKey == SortKey::Custom)
            settings.remove("sort/" + file);
        else
            settings.setValue("sort/" + file, QStringList{QString::number(int(m_sortKey)),
                                                          m_sortDescending ? "desc" : "asc",
                                                          QString::number(m_shuffleSeed)});
    }
    applySort();
}

void MainWindow::applySort()
{
    QVector<QListWidgetItem *> items;
    while (m_songs->count() > 0)
        items << m_songs->takeItem(0);
    const QCollator collator = [] { QCollator c; c.setNumericMode(true); c.setCaseSensitivity(Qt::CaseInsensitive); return c; }();
    auto seconds = [this](const QListWidgetItem *item) {
        return m_resolver.durationFor(idForTarget(item->data(Qt::UserRole).toString()));
    };
    auto entry = [](const QListWidgetItem *item) { return item->data(tiles::kEntryRole).toInt(); };
    const SortKey key = m_sortKey;
    const bool down = m_sortDescending;
    // A shuffled list keeps its order across visits: the same seed gives the
    // same permutation of the file's entries.
    QVector<int> rank(items.size());
    if (key == SortKey::Shuffled) {
        std::iota(rank.begin(), rank.end(), 0);
        std::shuffle(rank.begin(), rank.end(), std::mt19937(m_shuffleSeed));
    }
    std::stable_sort(items.begin(), items.end(), [&](const QListWidgetItem *a, const QListWidgetItem *b) {
        int c = 0;
        switch (key) {
        case SortKey::Title:
            c = collator.compare(a->text(), b->text());
            break;
        case SortKey::Artist:
            c = collator.compare(a->data(tiles::kArtistRole).toString(), b->data(tiles::kArtistRole).toString());
            if (c == 0)
                c = collator.compare(a->text(), b->text());
            break;
        case SortKey::Duration: {
            // Lengths still unknown go last either way.
            const int x = seconds(a), y = seconds(b);
            if ((x > 0) != (y > 0))
                return x > 0;
            c = x - y;
            break;
        }
        case SortKey::Added: {
            // Songs added here carry their own moment; the bot's arrive
            // together with the list, and among those the file's order is
            // the order they came in.
            const QDateTime x = a->data(tiles::kAddedAtRole).toDateTime(), y = b->data(tiles::kAddedAtRole).toDateTime();
            c = x < y ? -1 : x > y ? 1 : 0;
            break;
        }
        case SortKey::Shuffled:
            c = rank.value(entry(a)) - rank.value(entry(b));
            break;
        case SortKey::Custom:
            break;
        }
        if (c == 0)
            c = entry(a) - entry(b);   // ties keep the file's order
        return down ? c > 0 : c < 0;
    });
    for (int row = 0; row < items.size(); ++row) {
        items[row]->setData(tiles::kNumberRole, row + 1);
        m_songs->addItem(items[row]);
    }

    for (const auto &[k, button] : m_sortButtons) {
        const bool active = k == m_sortKey && k != SortKey::Custom;
        if (!button->property("label").toString().isEmpty())
            button->setText(button->property("label").toString() +
                            (active ? (m_sortDescending ? QStringLiteral("  ▾") : QStringLiteral("  ▴")) : QString()));
        button->setProperty("active", active);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
    if (m_pageShuffle) {
        auto *b = static_cast<icons::IconButton *>(m_pageShuffle);
        const bool on = m_sortKey == SortKey::Shuffled;
        b->setColors(on ? theme::accentText() : theme::c().dim, on ? theme::accentText().lighter(120) : theme::c().strong);
        b->setDot(on);
    }
    filterSongs();
    markPlaying();
}

void MainWindow::filterSongs()
{
    const QString needle = m_songSearch->text().trimmed();
    int shown = 0;
    for (int row = 0; row < m_songs->count(); ++row) {
        QListWidgetItem *item = m_songs->item(row);
        const bool hide = !needle.isEmpty() &&
            !item->text().contains(needle, Qt::CaseInsensitive) &&
            !item->data(tiles::kArtistRole).toString().contains(needle, Qt::CaseInsensitive);
        item->setHidden(hide);
        shown += hide ? 0 : 1;
    }
    // An empty list says how to fill it; a search that hides every song
    // says so instead of leaving a blank page.
    m_emptyHint->setText(m_songs->count() == 0
        ? QStringLiteral("Nothing here yet.\nRight-click a song in any playlist and choose Add to playlist.")
        : QStringLiteral("No songs match \"%1\".").arg(needle));
    m_emptyHint->setVisible(shown == 0);
    // A drag moves songs in the file, so only where the rows are the file's
    // own order, all of them shown.
    const bool canDrag = m_sortKey == SortKey::Custom && needle.isEmpty() && !m_videoGrid;
    m_songs->setDragDropMode(canDrag ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
}

QVector<int> MainWindow::shownOrder() const
{
    QVector<int> order;
    for (int row = 0; row < m_songs->count(); ++row)
        order << m_songs->item(row)->data(tiles::kEntryRole).toInt();
    return order;
}

void MainWindow::saveSongOrder()
{
    if (m_open < 0 || m_sortKey != SortKey::Custom)
        return;
    const QVector<int> order = shownOrder();
    bool moved = false;
    for (int i = 0; i < order.size(); ++i)
        moved |= order[i] != i;
    if (!moved)
        return;
    const QString path = m_library.playlists()[m_open].path;
    QFile before(path);
    if (before.open(QIODevice::ReadOnly)) {
        m_undoPath = path;
        m_undoBytes = before.readAll();
    }
    if (!Playlist::writeOrder(path, order))
        QMessageBox::warning(this, "Ghost Engine", "The playlist file couldn't be written.");
    rescan();
}

void MainWindow::removeSelectedSongs()
{
    if (m_open < 0 || m_songs->selectedItems().isEmpty())
        return;
    QSet<int> gone;
    for (QListWidgetItem *item : m_songs->selectedItems())
        gone << item->data(tiles::kEntryRole).toInt();
    const Playlist &p = m_library.playlists()[m_open];
    QVector<int> keep;
    for (int i = 0; i < p.count; ++i)
        if (!gone.contains(i))
            keep << i;
    QFile before(p.path);
    if (before.open(QIODevice::ReadOnly)) {
        m_undoPath = p.path;
        m_undoBytes = before.readAll();
    }
    if (!Playlist::writeOrder(p.path, keep))
        QMessageBox::warning(this, "Ghost Engine", "The playlist file couldn't be written.");
    rescan();
}

void MainWindow::addSelectedSongsTo(const QString &path)
{
    // Songs the list already has are not added twice.
    const Playlist target = Playlist::peek(path);
    QSet<QString> has;
    for (const PlaylistEntry &e : target.readEntries())
        has << e.target;
    const QVector<PlaylistEntry> all = m_library.playlists().value(m_open).readEntries();
    QVector<PlaylistEntry> adding;
    QList<QListWidgetItem *> picked = m_songs->selectedItems();
    std::sort(picked.begin(), picked.end(), [this](QListWidgetItem *a, QListWidgetItem *b) { return m_songs->row(a) < m_songs->row(b); });
    for (QListWidgetItem *item : picked) {
        const PlaylistEntry e = all.value(item->data(tiles::kEntryRole).toInt());
        if (!e.target.isEmpty() && !has.contains(e.target)) {
            adding << e;
            has << e.target;
        }
    }
    if (adding.isEmpty()) {
        toast(QStringLiteral("Already in %1").arg(target.name));
        return;
    }
    if (!Playlist::append(path, adding)) {
        QMessageBox::warning(this, "Add to playlist", "The playlist file couldn't be written.");
        return;
    }
    toast(adding.size() == 1 ? QStringLiteral("Added to %1").arg(target.name)
                             : QStringLiteral("%1 songs added to %2").arg(adding.size()).arg(target.name));
    rescan();
}

void MainWindow::newPlaylist(const QString &folder, bool addSelected, bool audio)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New playlist", "Playlist name:", QLineEdit::Normal,
                                               QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    const QString path = m_library.createPlaylist(name, folder, false, audio);
    if (path.isEmpty()) {
        QMessageBox::warning(this, "New playlist", "The playlist couldn't be made.");
        return;
    }
    if (addSelected && m_open >= 0 && !m_songs->selectedItems().isEmpty()) {
        addSelectedSongsTo(path);   // stays on the list the songs came from
        return;
    }
    if (!folder.isEmpty()) {
        m_openFolders.insert(folder);
        saveOpenFolders();
    }
    reopenAfter(path);
}

void MainWindow::toast(const QString &text, int ms)
{
    // A short note at the bottom of the window, gone by itself.
    if (!m_toast) {
        m_toast = new QLabel(this);
        m_toast->setObjectName("toast");
        m_toast->setAlignment(Qt::AlignCenter);
        m_toastTimer.setSingleShot(true);
        connect(&m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);
    }
    m_toast->setText(text);
    m_toast->adjustSize();
    m_toast->resize(m_toast->width() + 36, m_toast->height() + 16);
    m_toast->move((width() - m_toast->width()) / 2, height() - m_nowPlaying->height() - m_toast->height() - 16);
    m_toast->show();
    m_toast->raise();
    m_toastTimer.start(ms);
}

void MainWindow::songFailed(int entryId)
{
    // Every song failing in a row is rarely the songs: YouTube wants proof
    // that this is no bot (or the connection is down), and mpv only moves
    // on in silence. Said once two different songs have failed, and said
    // again with each further one, so it stays up for as long as songs keep
    // failing; the first song that opens takes it away.
    if (entryId < 0 || m_failedSongs.contains(entryId))
        return;
    m_failedSongs.insert(entryId);
    if (m_failedSongs.size() < 2)
        return;
    m_failNoticeShown = true;
    const bool haveCookies = QFileInfo::exists(QFileInfo(findMpv()).absoluteDir().filePath("cookies.txt"));
    if (!mpvconf::option("ghost-cookies-browser").isEmpty()) {
        toast(QStringLiteral("Songs aren't opening, even with your browser's cookies.\n"
                             "Check that you're signed in to YouTube in that browser."), 20000);
        return;
    }
    toast(haveCookies
        ? QStringLiteral("Songs aren't opening, even with your cookies.txt.\n"
                         "It has probably expired: export a fresh one and put it next to mpv.exe.")
        : QStringLiteral("Songs aren't opening. If your internet works, YouTube is probably checking for bots.\n"
                         "A cookies.txt next to mpv.exe gets past it: Settings > YouTube sign-in."),
        20000);
}

void MainWindow::checkForUpdates(bool asked)
{
    m_updateAsked = m_updateAsked || asked;
    QSettings().setValue("updates/checkedAt", QDateTime::currentDateTime());
    m_updates->check();
}

void MainWindow::showUpdates()
{
    if (m_updates->parts().isEmpty()) {
        checkForUpdates(true);
        return;
    }
    // Up to date or not, the same window: it lists what is installed.
    UpdateDialog dialog(m_updates->parts(), this);
    dialog.onStopPlayer = [this] {
        if (m_mpv.connected()) {
            m_mpv.command({"quit"});
            m_mpv.flush();
        }
    };
    dialog.onQuitForUpdate = [this] { quitForReal(); };
    dialog.exec();
    // What was updated in place is current now; ask again for the button.
    if (!m_quitting)
        checkForUpdates(false);
}

// The app's icon as the exe carries it (ghostengine.rc), at the sizes a tray
// draws: Qt's window icon comes from there too, but not as a QIcon.
static QIcon exeIcon()
{
    QIcon icon;
    for (int size : {16, 20, 24, 32, 48}) {
        HICON h = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), L"IDI_ICON1", IMAGE_ICON, size, size, 0));
        if (h) {
            icon.addPixmap(QPixmap::fromImage(QImage::fromHICON(h)));
            DestroyIcon(h);
        }
    }
    return icon;
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    QApplication::setQuitOnLastWindowClosed(false);   // closeEvent quits instead
    m_tray = new QSystemTrayIcon(exeIcon(), this);
    m_tray->setToolTip("Ghost Engine");
    // Our own menu rather than setContextMenu's Windows one, so it looks like
    // the app: opened on a right click (Context), a left click shows or hides.
    m_trayMenu = new TrayMenu(this);
    m_trayMenu->onPrevious = [this] { m_mpv.command({"playlist-prev"}); };
    m_trayMenu->onPlayPause = [this] { togglePause(); };
    m_trayMenu->onNext = [this] { m_mpv.command({"playlist-next"}); };
    m_trayMenu->onOpen = [this] { bringToFront(); };
    m_trayMenu->onCheckUpdates = [this] { checkForUpdates(true); };
    m_trayMenu->onThanks = [this] { showThanks(); };
    m_trayMenu->onQuit = [this] { quitForReal(); };
    m_trayMenu->onKeepRunning = [](bool on) { QSettings().setValue("tray/keepRunning", on); };
    m_trayMenu->setKeepRunning(QSettings().value("tray/keepRunning", false).toBool());
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason why) {
        if (why == QSystemTrayIcon::Context) {
            updateTray();
            m_trayMenu->popupAt(QCursor::pos());
        } else if (why == QSystemTrayIcon::Trigger) {
            if (isVisible() && !isMinimized())
                hide();
            else
                bringToFront();
        }
    });
    updateTray();
    m_tray->show();
}

void MainWindow::updateTray()
{
    if (!m_tray)
        return;
    const QString title = m_mpv.connected() ? m_mpv.property("media-title").toString() : QString();
    const QString artist = m_mpv.connected() ? m_mpv.property("metadata/by-key/uploader").toString() : QString();
    m_trayMenu->setNowPlaying(title, artist, !m_mpv.connected() || m_mpv.property("pause").toBool());
    m_tray->setToolTip(title.isEmpty() ? QStringLiteral("Ghost Engine")
                                       : QStringLiteral("Ghost Engine\n") + title.left(100));
}

void MainWindow::showThanks()
{
    // What Ghost Engine stands on, each with where it lives.
    QMessageBox box(this);
    box.setWindowTitle("Thanks to");
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral(
        "<b>Ghost Engine " GHOST_VERSION "</b> by TheTakanosu stands on the work of:<br><br>"
        "<a href='https://mpv.io'>mpv</a>, the player, in "
        "<a href='https://github.com/shinchiro/mpv-winbuild-cmake'>shinchiro's Windows builds</a><br>"
        "<a href='https://github.com/yt-dlp/yt-dlp'>yt-dlp</a>, which turns YouTube links into music<br>"
        "<a href='https://www.qt.io'>Qt</a>, which draws this app<br>"
        "<a href='https://lrclib.net'>LRCLIB</a>, for the lyrics<br>"
        "<a href='https://nodejs.org'>Node.js</a>, for YouTube's JavaScript checks<br>"
        "<a href='https://github.com/qwertyquerty/pypresence'>pypresence</a>, for the Discord card<br><br>"
        "And everyone who tried it early and said what was wrong."));
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.exec();
}

void MainWindow::undoSongEdit()
{
    if (m_undoPath.isEmpty() || m_open < 0 || m_library.playlists()[m_open].path != m_undoPath)
        return;
    QFile file(m_undoPath);
    const QDateTime modified = QFileInfo(m_undoPath).lastModified();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(m_undoBytes);
    file.setFileTime(modified, QFileDevice::FileModificationTime);
    file.close();
    m_undoPath.clear();
    m_undoBytes.clear();
    rescan();
}

void MainWindow::playOpen(int row, bool shuffle)
{
    // The row as shown; mpv plays the file, so it is told the entry, and the
    // order the rows are shown in when that is not the file's.
    if (m_open < 0) {
        playLog(QStringLiteral("playOpen row %1: no list open").arg(row));
        return;
    }
    const QVector<int> order = shownOrder();
    const int entry = order.value(row, 0);
    const bool sorted = m_sortKey != SortKey::Custom;
    play(m_library.playlists()[m_open].path, shuffle, entry, sorted && !shuffle ? order : QVector<int>{});
}

void MainWindow::applyPendingOrder()
{
    // Once mpv has expanded the .m3u, move its entries into the shown order.
    // Moving never restarts the song that is playing.
    if (m_pendingOrder.isEmpty() || !m_mpv.connected())
        return;
    // While mpv opens the .m3u itself, "path" is the list; once it is a song
    // the list has been expanded and the count is the new one, not the last.
    const QString list = QFileInfo(m_pendingPath).absoluteFilePath();
    if (QFileInfo(m_mpv.property("playlist-path").toString()).absoluteFilePath().compare(list, Qt::CaseInsensitive) != 0 ||
        QFileInfo(m_mpv.property("path").toString()).absoluteFilePath().compare(list, Qt::CaseInsensitive) == 0 ||
        m_mpv.property("playlist-count").toInt() != m_pendingOrder.size())
        return;
    QVector<int> at(m_pendingOrder.size());
    std::iota(at.begin(), at.end(), 0);   // which entry sits at each place now
    for (int place = 0; place < m_pendingOrder.size(); ++place) {
        const int from = at.indexOf(m_pendingOrder[place]);
        if (from != place) {
            m_mpv.command({"playlist-move", from, place});
            at.move(from, place);
        }
    }
    m_pendingOrder.clear();
}

// -- content -------------------------------------------------------------------

void MainWindow::rescan()
{
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    const QString openPath = m_open >= 0 ? m_library.playlists().value(m_open).path : QString();
    m_library.rescan();
    m_watcher.addPaths(m_library.watchPaths());
    m_open = -1;
    refill();
    const auto &all = m_library.playlists();
    for (int i = 0; i < all.size(); ++i)
        if (all[i].path == openPath)
            openPlaylist(i, true);
    for (const Playlist &p : all)
        if (m_offline.isMarked(p.path))
            m_offline.topUp(p.path, targetsOf(p));
}

QStringList MainWindow::targetsOf(const Playlist &p) const
{
    QStringList targets;
    for (const PlaylistEntry &e : p.readEntries())
        targets << e.target;
    return targets;
}

void MainWindow::toggleDownload(int index)
{
    const auto &all = m_library.playlists();
    if (index < 0 || index >= all.size())
        return;
    const Playlist p = all[index];
    if (!m_offline.isMarked(p.path)) {
        m_offline.download(p.path, targetsOf(p));
        toast(QStringLiteral("Downloading %1").arg(p.name));
        return;
    }
    if (QMessageBox::question(this, "Remove download",
            QStringLiteral("Remove the downloaded songs of \"%1\"? They play from YouTube again.").arg(p.name)) != QMessageBox::Yes)
        return;
    m_offline.remove(p.path, targetsOf(p), [this](const QString &file) {
        for (const Playlist &other : m_library.playlists())
            if (QFileInfo(other.path).fileName() == file)
                return targetsOf(other);
        return QStringList();
    });
}

void MainWindow::showDownloadState()
{
    // Off: grey. Downloading: violet, with how many are left. Done: violet.
    if (!m_downloadButton || m_open < 0)
        return;
    const QString path = m_library.playlists()[m_open].path;
    const bool marked = m_offline.isMarked(path);
    const int left = m_offline.pending(path);
    theme::style(m_downloadButton, marked ? QStringLiteral("color: @accentText;") : QString());
    m_downloadButton->setToolTip(!marked ? QStringLiteral("Download for offline listening")
                                 : left > 0 ? QStringLiteral("Downloading, %1 to go. Click to remove the download.").arg(left)
                                            : QStringLiteral("Downloaded. Click to remove the download."));
}

void MainWindow::refill()
{
    const auto &all = m_library.playlists();
    int songLists = 0, items = 0, fresh = 0;
    const QDateTime weekAgo = QDateTime::currentDateTime().addDays(-7);
    for (const Playlist &p : all) {
        songLists += p.audio ? 1 : 0;
        items += p.count;
        fresh += p.modified > weekAgo ? 1 : 0;
    }
    m_chips->button(AllLists)->setProperty("count", int(all.size()));
    m_chips->button(Songs)->setProperty("count", songLists);
    m_chips->button(Videos)->setProperty("count", int(all.size()) - songLists);
    fitLibraryHead();

    // library panel
    const QString needle = m_librarySearch ? m_librarySearch->text().trimmed() : QString();
    m_libraryList->clear();
    auto shown = [&](const Playlist &p) {
        return !((m_filter == Songs && !p.audio) || (m_filter == Videos && p.audio)) &&
               (needle.isEmpty() || p.name.contains(needle, Qt::CaseInsensitive));
    };
    const QStringList pinned = QSettings().value("library/pinned").toStringList();
    QHash<QString, int> byKey;
    for (int i = 0; i < all.size(); ++i)
        byKey.insert(QFileInfo(all[i].path).fileName(), i);
    auto addList = [&](int i, bool inFolder) {
        const Playlist &p = all[i];
        const QString key = QFileInfo(p.path).fileName();
        auto *row = new QListWidgetItem(p.name);
        row->setData(tiles::kIndexRole, i);
        row->setData(tiles::kKeyRole, key);
        row->setData(tiles::kPinnedRole, needle.isEmpty() && pinned.contains(key));
        row->setData(tiles::kSubtitleRole, m_rowDelegate->dense() ? QStringLiteral("Playlist") : subtitleFor(p));
        row->setData(tiles::kInFolderRole, inFolder);
        row->setFlags(row->flags() & ~Qt::ItemIsDropEnabled);
        row->setToolTip(p.name);
        m_libraryList->addItem(row);
        if (i == m_open)
            m_libraryList->setCurrentItem(row);
    };
    // A search looks through every list, in a folder or not, so nothing it
    // finds hides; the order is then the newest first, and nothing drags.
    m_libraryList->setDragDropMode(needle.isEmpty() ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
    if (!needle.isEmpty()) {
        // Lists by name first, then lists holding a song (title or artist)
        // that matches, each saying which song.
        QVector<int> bySong;
        QHash<int, QStringList> songsIn;
        for (int i = 0; i < all.size(); ++i) {
            const Playlist &p = all[i];
            if (shown(p)) {
                addList(i, false);
                continue;
            }
            if ((m_filter == Songs && !p.audio) || (m_filter == Videos && p.audio))
                continue;
            auto cached = m_titleCache.constFind(p.path);
            if (cached == m_titleCache.constEnd() || cached->first != p.modified) {
                QStringList titles;
                for (const PlaylistEntry &entry : p.readEntries())
                    titles << entry.title;
                cached = m_titleCache.insert(p.path, {p.modified, titles});
            }
            QStringList found;
            for (const QString &title : cached->second)
                if (title.contains(needle, Qt::CaseInsensitive))
                    found << title;
            if (!found.isEmpty()) {
                bySong << i;
                songsIn.insert(i, found);
            }
        }
        for (int i : bySong) {
            addList(i, false);
            QListWidgetItem *row = m_libraryList->item(m_libraryList->count() - 1);
            const QStringList &found = songsIn[i];
            const QString first = splitTitle(found.first()).first;
            row->setData(tiles::kSubtitleRole, found.size() == 1
                ? QStringLiteral("Song: %1").arg(first)
                : QStringLiteral("%1 songs · %2").arg(found.size()).arg(first));
            row->setData(tiles::kMatchRole, needle);
            row->setToolTip(found.mid(0, 8).join('\n') + (found.size() > 8 ? QStringLiteral("\n...") : QString()));
        }
        m_libraryEmpty->setText(QStringLiteral("No playlists or songs match \"%1\".").arg(needle));
    } else {
        // Folders, then lists newest first, until the user places them.
        QStringList top;
        for (const QString &folder : m_library.subfolders())
            top << QStringLiteral("folder:") + folder;
        for (int i = 0; i < all.size(); ++i)
            if (all[i].folder.isEmpty() && shown(all[i]))
                top << QFileInfo(all[i].path).fileName();
        for (const QString &key : arranged(top, QString())) {
            if (!key.startsWith(QStringLiteral("folder:"))) {
                addList(byKey.value(key), false);
                continue;
            }
            const QString folder = key.mid(7);
            QStringList inside;
            for (int i = 0; i < all.size(); ++i)
                if (all[i].folder == folder && shown(all[i]))
                    inside << QFileInfo(all[i].path).fileName();
            const bool open = m_openFolders.contains(folder);
            auto *row = new QListWidgetItem(folder);
            row->setData(tiles::kIndexRole, -1);
            row->setData(tiles::kKeyRole, key);
            row->setData(tiles::kPinnedRole, pinned.contains(key));
            row->setData(tiles::kFolderRole, folder);
            row->setData(tiles::kOpenRole, open);
            row->setData(tiles::kSubtitleRole, QStringLiteral("Folder · %1 %2").arg(inside.size())
                                                   .arg(inside.size() == 1 ? "playlist" : "playlists"));
            row->setFlags(row->flags() | Qt::ItemIsDropEnabled);   // a list dropped on it goes in
            row->setToolTip(folder);
            m_libraryList->addItem(row);
            if (open)
                for (const QString &k : arranged(inside, folder))
                    addList(byKey.value(k), true);
        }
    }
    m_libraryEmpty->setVisible(!needle.isEmpty() && m_libraryList->count() == 0);

    // Home
    m_greeting->setText(greetingFor(QTime::currentTime()));
    m_statLists->setText(QString::number(all.size()));
    m_statSongs->setText(QString::number(items));
    m_statHours->setText(QString::number((items * kMinutesPerSong + 30) / 60));
    m_statNew->setText(QString::number(fresh));
    m_welcome->setVisible(all.isEmpty());

    QVector<int> newest;
    for (int i = 0; i < all.size() && newest.size() < kShelfCount; ++i)
        newest << i;   // the library is already newest first
    fillShelf(m_recentAdded, newest);

    QVector<int> played;
    for (const QString &path : QSettings().value("recent/played").toStringList()) {
        for (int i = 0; i < all.size(); ++i)
            if (all[i].path.compare(path, Qt::CaseInsensitive) == 0) { played << i; break; }
        if (played.size() >= kShelfCount)
            break;
    }
    fillShelf(m_recentPlayed, played);
    m_recentPlayedTitle->setVisible(!played.isEmpty());
    m_recentPlayed->setVisible(!played.isEmpty());
}

void MainWindow::fillShelf(QListWidget *shelf, const QVector<int> &indices)
{
    const auto &all = m_library.playlists();
    shelf->clear();
    for (int i : indices) {
        auto *tile = new QListWidgetItem(all[i].name);
        tile->setData(tiles::kIndexRole, i);
        tile->setData(tiles::kSubtitleRole, subtitleFor(all[i]));
        shelf->addItem(tile);
    }
}

// -- covers --------------------------------------------------------------------

QString MainWindow::idForTarget(const QString &target)
{
    // An entry's own video, or the one songmatch.lua matched for its search,
    // or - for songs nobody has played yet - a lookup made for the picture.
    QString id = youtube::videoId(target);
    if (!id.isEmpty())
        return id;
    const QString query = youtube::searchQuery(target);
    id = m_matches.idFor(query);
    return id.isEmpty() ? m_resolver.idFor(query) : id;
}

QString MainWindow::customCoverPath(const QString &playlistPath)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
           QStringLiteral("/playlist-covers/") + QFileInfo(playlistPath).completeBaseName() + QStringLiteral(".jpg");
}

QVector<QPixmap> MainWindow::coversFor(int index)
{
    const auto &all = m_library.playlists();
    QVector<QPixmap> covers;
    if (index < 0 || index >= all.size())
        return covers;
    // Painted for every row on every repaint, so the disk is asked once.
    const QString custom = customCoverPath(all[index].path);
    auto cached = m_customCovers.constFind(custom);
    if (cached == m_customCovers.constEnd())
        cached = m_customCovers.insert(custom, QFileInfo::exists(custom) ? QPixmap(custom) : QPixmap());
    if (!cached->isNull())
        return {*cached};
    // Liked songs has a cover of its own, not its first song's, until one is
    // chosen for it (Batın, 2026-11-11).
    // Painted for every row on every repaint: the path is read once, and
    // compared as text (QFileInfo's == asks the disk).
    if (!m_likedKnown) {
        m_likedFile = QDir::cleanPath(QSettings().value("liked/path").toString());
        m_likedKnown = true;
    }
    if (!m_likedFile.isEmpty() && m_likedFile.compare(QDir::cleanPath(all[index].path), Qt::CaseInsensitive) == 0) {
        const QColor accent = theme::accent();
        if (m_likedCover.isNull() || m_likedAccent != accent) {
            m_likedCover = tiles::likedCover(640, accent);
            m_likedAccent = accent;
        }
        return {m_likedCover};
    }
    // A saved radio: the ghost on its first song's colour, as on the radio
    // page. Lists saved before the mark are known by their name.
    static const QRegularExpression radioName(QStringLiteral(R"(^Ghost Engine Radio #\d+$)"));
    if (all[index].radio || radioName.match(all[index].name).hasMatch()) {
        const QStringList first = coverIds(all[index], 1);
        const QColor found = first.isEmpty() ? QColor()
                                             : tiles::coverColor(m_covers.get(first.first(), ThumbnailCache::Size::Card));
        const QColor base = found.isValid() ? found : theme::accent();
        RadioCover &drawn = m_radioCovers[all[index].path];
        if (drawn.picture.isNull() || drawn.colour != base) {
            drawn.picture = tiles::radioCover(640, base);
            drawn.colour = base;
        }
        return {drawn.picture};
    }
    for (const QString &id : coverIds(all[index], 4))
        covers << m_covers.get(id, ThumbnailCache::Size::Card);
    return covers;
}

QStringList MainWindow::coverIds(const Playlist &p, int wanted)
{
    QStringList ids;
    for (const QString &target : p.preview) {
        const QString id = idForTarget(target);
        if (!id.isEmpty() && !ids.contains(id))
            ids << id;
        if (ids.size() >= wanted)
            break;
    }
    return ids;
}

// -- actions -------------------------------------------------------------------

QString MainWindow::playingListPath() const
{
    const QString mpvSays = m_mpv.property("playlist-path").toString();
    if (mpvSays.isEmpty())
        // Queued, Autoplay's, or a lone video's suggestions (added to its
        // file and to mpv's list by hand, so mpv names no list for them):
        // the list they came after. Without it the video page stayed on the
        // first video while the next played (seen, 2026-11-20).
        return m_mpv.connected() && (playingAdded() || m_suggestedTitles.contains(m_mpv.property("path").toString()))
                   ? m_lastListPath : QString();
    const QFileInfo info(mpvSays);
    QString path = info.absoluteFilePath();
    if (!info.exists())
        for (const Playlist &p : m_library.playlists())
            if (QFileInfo(p.path).fileName().compare(info.fileName(), Qt::CaseInsensitive) == 0) {
                path = QFileInfo(p.path).absoluteFilePath();
                break;
            }
    m_lastListPath = path;
    return path;
}

QString MainWindow::findMpv() const
{
    // The installer puts the app in the mpv folder; C:\mpv is its default.
    for (const QString &dir : {QCoreApplication::applicationDirPath(), QStringLiteral("C:/mpv")}) {
        const QString exe = QDir(dir).filePath("mpv.exe");
        if (QFileInfo::exists(exe))
            return exe;
    }
    return {};
}

void MainWindow::play(const QString &path, bool shuffle, int startAt, const QVector<int> &order)
{
    rememberPlayed(path);
    m_nowPlaying->expectList(shuffle);
    m_miniClosed = false;
    m_pictureAway = false;
    m_pendingPath = path;
    m_pendingOrder = order;
    // A list that never loads should not reorder a later one.
    const int serial = ++m_pendingSerial;
    if (!order.isEmpty())
        QTimer::singleShot(15000, this, [this, serial] { if (m_pendingSerial == serial) m_pendingOrder.clear(); });
    // An mpv already open takes the list over IPC, so a second window does
    // not start on top of the first.
    playLog(QStringLiteral("play %1 at %2, connected %3, launching %4 ms, pid %5")
                .arg(QFileInfo(path).fileName()).arg(startAt).arg(m_mpv.connected())
                .arg(m_launching.isValid() ? m_launching.elapsed() : -1).arg(m_launchPid));
    // A Play while the mpv just started is still opening waits for it; but
    // not for one that ended before it answered: that one never will, and
    // every Play for the next 15 seconds did nothing.
    if (!m_mpv.connected() && m_launching.isValid() && m_launching.elapsed() < 15000) {
        if (m_launchPid > 0 && processAlive(m_launchPid)) {
            m_queuedPlay = QueuedPlay{path, shuffle, startAt, order};   // the latest click wins
            playLog(QStringLiteral("  queued until mpv answers"));
            return;
        }
        playLog(QStringLiteral("  the mpv started last is gone; starting another"));
        m_launching.invalidate();
    }
    const bool audio = listIsAudio(path);
    const int embed = embedFor(audio);
    if (m_mpv.connected()) {
        // An mpv drawing in the wrong place (in the app for a song list that
        // wants its own window, or outside for a video) is started again.
        if (embed != -1 && bool(embed) != m_mpvEmbedded) {
            restartPlayer(QueuedPlay{path, shuffle, startAt, order}, 0);
            return;
        }
        // A list chosen now starts at 0, whatever a reload left set; a tab
        // (or a resume) at its second, for that entry only.
        m_resetStart = false;
        if (m_startSeconds > 1) {
            m_resumePath = QStringLiteral("*");   // the entry that loads next
            m_resumeClock.start();
            m_mpv.setProperty("start", QString::number(m_startSeconds, 'f', 1));
            armStartReset(startAt);
        } else {
            m_mpv.setProperty("start", "none");
        }
        m_startSeconds = 0;
        const bool paused = m_loadPaused;
        m_loadPaused = false;
        if (paused)
            m_mpv.setProperty("pause", true);   // before it loads: it never starts
        m_mpv.command({"loadfile", path, "replace", -1, QStringLiteral("playlist-start=%1").arg(startAt)});
        if (shuffle)
            QTimer::singleShot(400, this, [this] { m_mpv.command({"playlist-shuffle"}); });
        if (!paused)
            m_mpv.setProperty("pause", false);
        if (!audio && m_mpvEmbedded)
            showVideoPage();
        return;
    }
    const QString mpv = findMpv();
    if (mpv.isEmpty()) {
        QMessageBox::warning(this, "Ghost Engine",
                             "mpv.exe wasn't found. Install Ghost Engine with install.bat first.");
        return;
    }
    QStringList args{QStringLiteral("--input-ipc-server=%1").arg(MpvClient::kPipe),
                     // No sound device: play on in silence, in real time; it
                     // ended every song at once and the list raced (2026-11-11).
                     QStringLiteral("--audio-fallback-to-null=yes")};
    if (!QSettings().value("player/window", false).toBool())
        args << QStringLiteral("--script-opts-append=ghost-window=no");
    if (shuffle)
        args << "--shuffle";
    if (m_loadPaused)
        args << "--pause";
    if (m_videoPage->listenOnly())
        args << QStringLiteral("--script-opts-append=ghost-listen=yes");
    m_loadPaused = false;
    if (startAt > 0)
        args << QStringLiteral("--playlist-start=%1").arg(startAt);
    if (m_startSeconds > 1) {
        args << QStringLiteral("--start=%1").arg(m_startSeconds, 0, 'f', 1);
        m_resumeClock.start();
        // --start is mpv's for every file it opens, not this one's: left
        // set, each later song began at that second, past the end of a short
        // one, and failed (Batin, 2026-11-12: nothing would play, every
        // song "could not be opened", mpv stuck at start=266.2).
        armStartReset(startAt);
    }
    m_startSeconds = 0;
    // Inside the app whenever videos may show there, so a video list later
    // does not need mpv started again. Keys go to the app (it passes them
    // on), mouse and mpv's own controls stay mpv's.
    m_launchEmbedded = embed == 1 || (embed == -1 && embedFor(false) == 1);
    if (m_launchEmbedded)
        args << QStringLiteral("--wid=%1").arg(qulonglong(m_videoPage->surface()->winId()))
             << "--input-vo-keyboard=no"
             // Drawn the classic way (blit) rather than handed to Windows'
             // compositor as a layer of its own (flip): as a layer, the cut
             // the app makes at the page's top edge reached it a frame late
             // and a strip of the video showed above the page while scrolling.
             << "--d3d11-flip=no";
    args << "--" << path;
    // From mpv's own folder, the way a double-click on the .m3u starts it, so
    // its scripts and the presence agent behave the same.
    qint64 pid = 0;
    if (QProcess::startDetached(mpv, args, QFileInfo(mpv).absolutePath(), &pid)) {
        playLog(QStringLiteral("  started mpv %1%2").arg(pid).arg(m_launchEmbedded ? " (in the app)" : ""));
        m_launchedPids << pid;
        m_launchPid = pid;
        if (m_launchEmbedded)
            tieToApp(pid);
        m_launching.start();
        m_mpv.hurry();
        if (!audio && m_launchEmbedded)
            showVideoPage();
        // Ended before it answered (it happens on a first start now and
        // then): a Play waiting for it is played by a new one, once.
        auto *watch = new QTimer(this);
        watch->setInterval(500);
        connect(watch, &QTimer::timeout, this, [this, watch, pid, tries = 0]() mutable {
            if (m_mpv.connected() || m_launchPid != pid || ++tries > 30) {
                watch->deleteLater();
                return;
            }
            if (processAlive(pid))
                return;
            watch->deleteLater();
            playLog(QStringLiteral("  mpv %1 ended before it answered").arg(pid));
            m_launching.invalidate();
            m_launchPid = 0;
            if (m_queuedPlay) {
                const QueuedPlay q = *m_queuedPlay;
                m_queuedPlay.reset();
                play(q.path, q.shuffle, q.startAt, q.order);
            }
        });
        watch->start();
    } else {
        playLog(QStringLiteral("  mpv could not be started: %1").arg(mpv));
        toast("mpv couldn't be started. Try again, or reinstall Ghost Engine.");
    }
}

bool MainWindow::listIsAudio(const QString &path) const
{
    for (const Playlist &p : m_library.playlists())
        if (p.path.compare(path, Qt::CaseInsensitive) == 0)
            return p.audio;
    return Playlist::peek(path).audio;
}

int MainWindow::embedFor(bool audio) const
{
    const bool videosInside = QSettings().value("player/embedVideo", true).toBool() && !m_videoPoppedOut;
    if (!audio)
        return videosInside ? 1 : 0;
    // A song list draws nothing unless it was asked to have a window; that
    // window must then be mpv's own, not one hidden in the app.
    return QSettings().value("player/window", false).toBool() ? 0 : -1;
}

void MainWindow::armStartReset(int entry)
{
    // mpv's `start` is one for all files: set to reload an entry at a
    // second, it is put back to none once that entry plays or another comes.
    m_resetStart = true;
    m_resetStartPos = entry;
}

void MainWindow::restartPlayer(const QueuedPlay &q, double seconds)
{
    if (q.path.isEmpty())
        return;
    m_startSeconds = seconds;
    if (!m_mpv.connected()) {
        play(q.path, q.shuffle, q.startAt, q.order);
        return;
    }
    m_restartPlay = q;   // started once the old mpv has gone
    m_mpv.command({"quit"});
}

void MainWindow::showVideoPage()
{
    updateVideoPage();
    m_videoPage->setPaused(m_mpv.property("pause").toBool());
    m_videoPage->setTracks(m_mpv.property("track-list").toList());
    m_videoPage->setSpeed(m_mpv.property("speed").isValid() ? m_mpv.property("speed").toDouble() : 1.0);
    static const QRegularExpression cap(QStringLiteral(R"(height<=(\d+))"));
    m_videoPage->setQualityCap(cap.match(m_mpv.property("ytdl-format").toString()).captured(1).toInt());
    m_videoPage->setVolume(int(m_mpv.property("volume").toDouble()), m_mpv.property("mute").toBool());
    m_videoPage->setCinema(QSettings().value("video/cinema", false).toBool());
    m_miniClosed = false;
    m_pictureAway = false;
    m_miniAsked = false;
    // A key here is a shortcut (I, F, Space), not a letter for the search
    // box the video was found with, which kept the focus.
    if (QWidget *focus = QApplication::focusWidget();
        focus && (qobject_cast<QLineEdit *>(focus) || qobject_cast<QTextEdit *>(focus)) && focus->window() == this)
        focus->clearFocus();
    if (m_pages->currentWidget() != m_videoPage) {
        m_beforeVideo = m_pages->currentWidget();
        m_videoPage->ignorePictureClicks();
    }
    m_pages->setCurrentWidget(m_videoPage);
    m_nowPlaying->setLyricsOpen(false);
    schedulePlace();
}

void MainWindow::setVideoGrid(bool grid)
{
    // Videos as cards, YouTube's way; songs as rows, Spotify's.
    if (grid != m_videoGrid) {
        m_videoGrid = grid;
        m_songs->setViewMode(grid ? QListView::IconMode : QListView::ListMode);
        m_songs->setItemDelegate(grid ? static_cast<QAbstractItemDelegate *>(m_cardDelegate) : m_rowDelegateSongs);
        m_songs->setMovement(QListView::Static);
        m_songs->setFlow(grid ? QListView::LeftToRight : QListView::TopToBottom);
        m_songs->setWrapping(grid);
        // Laid out again by fitVideoGrid on every resize, at once: the view's
        // own Adjust waits 100 ms, and while a panel's edge was dragged the
        // cards stood in the old flow (two columns where three now fit),
        // jumping back and forth (filmed).
        m_songs->setResizeMode(QListView::Fixed);
        if (!grid)
            m_songs->setGridSize(QSize());
        m_columnsBar->setVisible(!grid);
    }
    fitVideoGrid();
    // Again once the page is laid out: opened from another page, the list's
    // width was still the old one and the cards stood at half the width.
    QTimer::singleShot(0, this, &MainWindow::fitVideoGrid);
}

void MainWindow::fitVideoGrid()
{
    // As many columns as fit at 360 px or more, sharing the width: the same
    // cards as the YouTube pages (2026-11-08).
    if (!m_videoGrid)
        return;
    // 8 px to spare: with 4 the view wrapped the last column whenever the
    // cards came to within 4 px of its edge (measured: 3 x 321 in 967 went
    // to two columns), and dragging an edge flicked between the two.
    const int width = m_songs->viewport()->width() - 8;
    const int columns = qMax(1, width / int(360 * theme::zoom()));
    m_cardDelegate->setCardWidth(width / columns);
    if (m_songs->gridSize() != m_cardDelegate->cardSize())
        m_songs->setGridSize(m_cardDelegate->cardSize());
    m_songs->doItemsLayout();
}

bool MainWindow::videoShowing() const
{
    return m_mpvEmbedded && m_mpv.connected() && !m_videoPoppedOut &&
           (!m_mpv.property("user-data/ghost/audio").toBool() || songVideo());
}

void MainWindow::placeWithPage()
{
    // While the page scrolls: the page is drawn first, then the picture is
    // put where it now is, one right after the other so both reach the
    // screen in the same frame. Placed before the page was drawn, or after
    // the next turn of the loop, one of the two was a frame behind, and the
    // picture jumped and settled.
    if (m_placeWithPage)
        return;
    m_placeWithPage = true;
    QMetaObject::invokeMethod(this, [this] {
        m_placeWithPage = false;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
        placeSurface();
    }, Qt::QueuedConnection);
}

void MainWindow::startSurfaceDrag()
{
    if (!videoShowing())
        return;
    if (!m_surfaceDrag) {
        m_surfaceDrag = true;
        // The frame on screen right now, held still while the edge moves (as
        // if paused for a moment); the thumbnail when the picture is partly
        // scrolled away and no whole frame is there to take.
        // Taken when the edge was pressed if it was: by the first move the
        // page had already been drawn moved over part of the picture, and a
        // fast drag held a frame with the picture twice in it.
        QPixmap still = std::exchange(m_pressedStill, QPixmap());
        if (still.isNull())
            still = grabStill();
        if (still.isNull()) {
            QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString());
            if (id.isEmpty())
                id = idForTarget(m_mpv.property("path").toString());
            still = m_covers.get(id, ThumbnailCache::Size::Hero);
        }
        m_videoPage->setStill(still);
        if (panelVideo())
            m_panel->setCover(still);
    }
    m_surfaceDragEnd.start();
}

QPixmap MainWindow::grabStill() const
{
    NativeSurface *surface = m_videoPage->surface();
    if (!surface || m_videoArea.isEmpty() || m_videoArea.size() != surface->geometry().size())
        return {};
    QScreen *screen = QGuiApplication::screenAt(m_videoArea.center());
    if (!screen)
        return {};
    return screen->grabWindow(0, m_videoArea.x() - screen->geometry().x(), m_videoArea.y() - screen->geometry().y(),
                              m_videoArea.width(), m_videoArea.height());
}

void MainWindow::schedulePlace()
{
    if (m_placeTimer)
        m_placeTimer->start();   // once, after the layout has settled
}

void MainWindow::placeSurface()
{
    NativeSurface *surface = m_videoPage->surface();
    if (!surface)
        return;
    QWidget *host = this;   // the main window, whose client area the surface is placed in
    // A pinned mini player stays while the app is minimised.
    const bool minimized = isMinimized();
    const bool video = videoShowing() && (!minimized || m_videoPage->miniPinned());
    const bool onPage = video && !minimized && m_pages->currentWidget() == m_videoPage;
    // "Mini video" plays where the now playing view's cover is, while the
    // view is open; else in the corner, as any video.
    const bool inPanel = video && !onPage && !minimized && panelVideo() && m_panel->isVisible();
    // Leaving the video's page for another page: the picture goes away and
    // the sound goes on, unless the corner player was asked for (its button
    // on the page, or Settings > Player).
    if (video && !onPage && !inPanel && m_surfaceOnPage && !m_miniAsked &&
        !QSettings().value("video/miniOnLeave", false).toBool())
        m_pictureAway = true;
    // Listening only: no picture to show, on the page or in the corner.
    // A tab's cover too: its picture is the cover until Play.
    const bool listen = (m_videoPage->listenOnly() && !m_mpv.property("user-data/ghost/audio").toBool()) ||
                        m_videoPage->coverShown();
    const bool mini = video && !onPage && !inPanel && !m_miniClosed && !m_pictureAway && !listen;
    QWidget *miniWindow = m_videoPage->miniWindow();
    if ((m_surfaceDrag || m_surfaceHeld) && (onPage || inPanel)) {
        // Put away until the edge is let go; the thumbnail stands in.
        surface->setVisible(false);
        m_videoPage->hideMini();
        m_videoPage->corners()->hide();
        m_videoPage->controls()->hide();
        m_surfaceOnPage = false;
        m_barFits = false;
        m_videoArea = QRect();
        return;
    }
    QRect visible;   // of the picture, in the host's coordinates
    m_videoPage->setWheelTarget(inPanel ? m_panel->scrollArea()->findChild<QWidget *>("qt_scrollarea_viewport") : nullptr);
    if (inPanel) {
        QWidget *cover = m_panel->coverArea();
        QWidget *scroll = m_panel->scrollArea();
        const QRect picture(host->mapFromGlobal(cover->mapToGlobal(QPoint())), cover->size());
        const QRect view(host->mapFromGlobal(scroll->mapToGlobal(QPoint())), scroll->size());
        visible = picture & view;
        m_videoPage->setSurfaceCorner(8);
        m_videoPage->setSurfaceVisibleArea(visible.isEmpty() ? QRect(0, 0, 1, 1) : visible.translated(-picture.topLeft()));
        surface->place(this, picture);
        m_videoPage->hideMini();
    } else if (onPage) {
        // The page scrolls the picture up and out of view: only what is
        // inside the page's scrolling area shows.
        QWidget *screen = m_videoPage->screen();
        m_videoPage->setSurfaceCorner(m_videoFull ? 0 : 12);
        const QRect picture(host->mapFromGlobal(screen->mapToGlobal(QPoint())), screen->size());
        const QRect view(host->mapFromGlobal(m_videoPage->viewport()->mapToGlobal(QPoint())), m_videoPage->viewport()->size());
        visible = picture & view;
        m_videoPage->setSurfaceVisibleArea(visible.isEmpty() ? QRect(0, 0, 1, 1) : visible.translated(-picture.topLeft()));
        surface->place(this, picture);
        m_videoPage->hideMini();
    } else if (mini) {
        // The video's shape, 384 px wide unless it was resized; bottom right
        // of the page until it is dragged somewhere else.
        const double aspect = qBound(0.5, m_mpv.property("video-params/aspect").isValid()
                                              ? m_mpv.property("video-params/aspect").toDouble() : 16.0 / 9.0, 2.6);
        const int w = m_miniWidth > 0 ? m_miniWidth : int(384 * theme::zoom());
        const QSize size(w, int(w / aspect));
        const QRect pages(m_pages->mapToGlobal(QPoint()), m_pages->size());
        QPoint at = m_miniAt.value_or(QPoint(pages.right() - size.width() - 16, pages.bottom() - size.height() - 16));
        if (const QScreen *screen = QGuiApplication::screenAt(at + QPoint(size.width() / 2, size.height() / 2)); !screen)
            at = QPoint(pages.right() - size.width() - 16, pages.bottom() - size.height() - 16);
        if (!miniWindow->isVisible())
            miniWindow->setGeometry(QRect(at, size));
        m_videoPage->setSurfaceCorner(0);   // the window's own corners are round
        m_videoPage->setSurfaceVisibleArea(QRect());
        // While a layout switch plays the corner player waits, hidden with
        // the picture in it: put away on its own the picture came back for
        // a moment in the window's top-left corner (Batin, 2026-11-08).
        surface->place(miniWindow, QRect(QPoint(0, 0), miniWindow->size()));
        miniWindow->setVisible(!m_surfaceHeld);
    } else {
        m_videoPage->hideMini();
    }
    const bool framed = m_frameWaitPath.isEmpty();
    surface->setVisible((onPage && !listen && framed) || (mini && !m_videoPage->miniSizing() && framed) ||
                        (inPanel && !visible.isEmpty()));
    m_surfaceOnPage = onPage || inPanel;   // the controls come over it on hover

    // mpv fills its window when the window already has the video's shape,
    // so no hairline of black is left at its edges; in full screen it keeps
    // the shape and draws its black around it.
    if (m_mpv.connected() && m_mpvEmbedded && (onPage || mini || inPanel)) {
        // On the page a video narrower than 16:9 sits in a 16:9 frame, its
        // shape kept with black at its sides.
        const double shape = m_mpv.property("video-params/aspect").isValid()
                                 ? m_mpv.property("video-params/aspect").toDouble() : 16.0 / 9.0;
        // Kept too while the shape is not yet known or the window is being
        // sized (opened, restored with Win+3): stretched to the frame for a
        // moment, then banded, it flickered (Batin, 2026-11-20).
        const bool known = m_mpv.property("video-params/aspect").isValid() && m_frameWaitPath.isEmpty();
        const int keep = onPage && (m_videoFull || !known || shape < 16.0 / 9.0 - 0.02) ? 1 : 0;
        if (keep != m_keepAspect) {
            m_keepAspect = keep;
            m_mpv.setProperty("keepaspect", bool(keep));
        }
    }

    const QRect global(host->mapToGlobal(surface->geometry().topLeft()), surface->geometry().size());
    m_videoArea = onPage || inPanel ? QRect(host->mapToGlobal(visible.topLeft()), visible.size()) : QRect();
    m_videoPage->setControlsCompact(inPanel);
    if (searchOpen() || (onPage && listen))   // "Back to video" takes the clicks there
        m_videoPage->corners()->hide();
    else if (onPage)
        m_videoPage->placeOverPicture(global, m_videoArea);
    else if (inPanel)   // click to pause, double-click for full screen there too
        m_videoPage->placeOverPicture(global, m_videoArea);
    else
        m_videoPage->corners()->hide();
    // The controls sit on the picture's bottom edge, so only while that is
    // in view.
    QWidget *controls = m_videoPage->controls();
    // Not over a tab's cover: those are the other video's controls
    // (Batin, 2026-11-20: its timeline could be dragged from a tab's page).
    m_barFits = (onPage || inPanel) && !m_videoArea.isEmpty() && m_videoArea.bottom() >= global.bottom() - 1 &&
                !m_videoPage->coverShown();
    if (m_barFits) {
        const int h = controls->sizeHint().height();
        controls->setGeometry(global.left(), global.bottom() + 1 - h, global.width(), h);
        if (searchOpen())
            controls->hide();
        else if (controls->isVisible())
            controls->raise();
    } else {
        controls->hide();
    }
    // The Up next card over the middle of what shows of the picture.
    m_videoPage->placeUpNext(onPage && !searchOpen() && !listen ? m_videoArea : QRect());
    // The song bar is for songs: on the video page the controls over the
    // picture stand in for it.
    // Nor on the YouTube side at all: the video's own controls and the
    // floating island do its work there, and the song bar made it look like
    // the music side (Batin, 2026-11-20).
    m_nowPlaying->setVisible(!m_youtubeLayout && !m_videoFull && !(m_pages->currentWidget() == m_videoPage && !m_videoPoppedOut));
}

bool MainWindow::openPlayingChannel(bool always)
{
    // A YouTube video (not a song of a song list): its name leads to its
    // channel on the YouTube side, not to a YouTube Music artist of a
    // similar name (Batin, 2026-11-20: "YusufTe 2" opened cakal).
    const QString id = !m_videoChannelId.isEmpty() ? m_videoChannelId : m_panelChannelId;
    // A song (or a song's video) has its artist's page instead.
    if (id.isEmpty() || (!always && m_mpv.property("user-data/ghost/audio").toBool()))
        return false;
    const QString name = m_mpv.property("metadata/by-key/uploader").toString();
    if (!m_youtubeLayout)
        setYouTubeLayout(true, !always);
    showYouTube();
    markYouTubeMenu(-1, -1, id);
    m_searchPage->showChannel(id, name);
    return true;
}

void MainWindow::offerNextVideo()
{
    // Opened at a kept second that was its very end: from its start, not
    // the Up next card a moment after Play (Batin, 2026-11-20).
    if (m_resumeClock.isValid() && m_resumeClock.elapsed() < 15000 && m_mpv.property("eof-reached").toBool() &&
        m_mpvEmbedded && !m_mpv.property("user-data/ghost/audio").toBool()) {
        m_resumeClock.invalidate();
        m_videoPage->hideUpNext();
        m_mpv.command({"seek", 0, "absolute"});
        m_mpv.setProperty("pause", false);
        return;
    }
    // A YouTube video ended (keep-open stopped it): the next entry offered
    // on the Up next card, counted down when Autoplay is on.
    if (!m_mpvEmbedded || m_mpv.property("user-data/ghost/audio").toBool() ||
        !m_mpv.property("eof-reached").toBool()) {
        m_videoPage->hideUpNext();
        return;
    }
    const QVariantList entries = m_mpv.property("playlist").toList();
    const int pos = m_mpv.property("playlist-pos").toInt();
    if (pos < 0 || pos + 1 >= entries.size()) {
        m_videoPage->hideUpNext();   // the last: nothing comes next
        return;
    }
    const QVariantMap next = entries[pos + 1].toMap();
    const QString target = next.value("filename").toString();
    QString title = next.value("title").toString();
    if (title.isEmpty())
        title = m_queueTitles.value(target, m_autoplayTitles.value(target, m_suggestedTitles.value(target)));
    if (title.isEmpty())
        title = target;
    const bool autoplay = QSettings().value("video/autoplay", false).toBool();
    m_videoPage->showUpNext(title, autoplay ? 8 : 0);
    schedulePlace();
}

bool MainWindow::searchOpen() const
{
    return m_searchPopup && m_searchPopup->isVisible();
}

void MainWindow::watchVideoMouse()
{
    // mpv's window takes the mouse, so the app does not hear it move there;
    // where it is gets looked at a few times a second instead. The controls
    // show while it moves on the picture and go once it has rested, unless
    // the video is paused or a menu of theirs is open.
    if (m_videoPage->surface())
        m_videoPage->surface()->keepOnTop();
    QWidget *controls = m_videoPage->controls();
    if (!m_surfaceOnPage || !m_barFits || searchOpen()) {
        controls->hide();
        return;
    }
    const QRect area = m_videoArea;
    const QPoint at = QCursor::pos();
    POINT pt;
    GetCursorPos(&pt);
    const HWND top = GetAncestor(WindowFromPoint(pt), GA_ROOT);
    // Not when another program's window is over it.
    const bool over = area.contains(at) &&
                      (top == reinterpret_cast<HWND>(winId()) || top == reinterpret_cast<HWND>(controls->winId()) ||
                       top == reinterpret_cast<HWND>(m_videoPage->corners()->winId()));
    if (at != m_lastVideoMouse) {
        m_lastVideoMouse = at;
        m_videoMouseStill = 0;
        if (over && !controls->isVisible()) {
            controls->show();
            controls->raise();
        }
    } else {
        ++m_videoMouseStill;
    }
    const bool onBar = controls->isVisible() && controls->geometry().contains(at);
    const bool keep = onBar || QApplication::activePopupWidget() || (over && m_videoPage->paused());
    if (controls->isVisible() && !keep && (!over || m_videoMouseStill >= 12))   // 2.4 seconds
        controls->hide();
}

void MainWindow::updateVideoPage()
{
    if (!m_videoPage || m_preview || (m_mpv.property("user-data/ghost/audio").toBool() && !songVideo()))
        return;
    const QString list = playingListPath();
    if (list.isEmpty())
        return;
    if (list != m_videoListPath) {
        m_videoListPath = list;
        Playlist p = Playlist::peek(list);
        QVector<VideoPage::Entry> entries;
        for (const PlaylistEntry &e : p.readEntries())
            entries.append({e.title, e.target});
        m_videoPage->setList(p.name, entries);
        if (entries.size() == 1)
            extendSingleVideo(list, youtube::videoId(entries.first().target));
    }
    const QString path = m_mpv.property("path").toString();
    QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString());
    if (id.isEmpty())
        id = youtube::videoId(path);
    m_videoPage->setPlaying(path, m_mpv.property("media-title").toString(),
                            m_mpv.property("metadata/by-key/uploader").toString(),
                            id.isEmpty() ? QString() : QStringLiteral("https://www.youtube.com/watch?v=") + id);
    m_videoPage->setAvailableHeight(0);   // until this video's sizes are known
    if (!id.isEmpty())
        describeVideo(id);
}

void MainWindow::extendSingleVideo(const QString &list, const QString &id)
{
    // One video played on its own (its menu's Play, or picked up again
    // later) had a side list of itself alone (Batin, 2026-11-20). As on
    // YouTube, what it suggests next comes after it: added to the end of the
    // list's file, which is one of the app's own (never a library list), and
    // of mpv's list, so nothing that plays is reloaded.
    if (id.isEmpty() || m_extendedLists.contains(list) ||
        !QFileInfo(list).absolutePath().startsWith(QFileInfo(foundFolder()).absoluteFilePath(), Qt::CaseInsensitive))
        return;
    m_extendedLists.insert(list);
    m_ytClient.related(id, [this, list, id](QVector<ytmusic::Item> items) {
        if (playingListPath() != list)
            return;
        QVector<PlaylistEntry> add;
        QSet<QString> seen{id};
        for (const ytmusic::Item &it : items) {
            if (it.id.isEmpty() || seen.contains(it.id))
                continue;
            seen.insert(it.id);
            add << PlaylistEntry{it.lineTitle(), it.target(), {}};
            if (add.size() >= 20)
                break;
        }
        if (add.isEmpty() || !Playlist::append(list, add))
            return;
        // mpv names them only from a list file: the panel's "Next in queue"
        // showed their address, so their names are kept here. They are the
        // list's own now (in its file), not Autoplay's, and count as such.
        for (const PlaylistEntry &e : add) {
            m_mpv.command({"loadfile", e.target, "append"});
            m_suggestedTitles.insert(e.target, e.title);
        }
        m_videoListPath.clear();   // read again, with them
        updateVideoPage();
    });
}

void MainWindow::describeVideo(const QString &id)
{
    // Only for videos, only once each: one yt-dlp call that downloads
    // nothing, after the video has started, so it never delays playing.
    // The first line yt-dlp prints is "views|upload date", the rest is the
    // description.
    auto show = [this, id](const QString &whole) {
        // The storyboard's formats come first (a JSON line), when asked for.
        QString printed = whole;
        if (printed.startsWith(QLatin1Char('['))) {
            const qsizetype end = printed.indexOf(QLatin1Char('\n'));
            const QString formats = printed.left(end < 0 ? printed.size() : end);
            printed = end < 0 ? QString() : printed.mid(end + 1);
            if (!m_preview || m_previewTab.id != id)
                m_storyboard.setVideo(id, formats.toUtf8());
        }
        const qsizetype newline = printed.indexOf('\n');
        const QStringList facts = printed.left(newline < 0 ? printed.size() : newline).split('|');
        const QString text = newline < 0 ? QString() : printed.mid(newline + 1);
        const QLocale english(QLocale::English);
        QStringList meta;
        bool counted = false;
        const qlonglong views = facts.value(0).toLongLong(&counted);
        if (counted)
            meta << english.toString(views) + (views == 1 ? " view" : " views");
        const QDate date = QDate::fromString(facts.value(1).trimmed(), QStringLiteral("yyyyMMdd"));
        if (date.isValid())
            meta << english.toString(date, QStringLiteral("MMM d, yyyy"));
        m_videoPage->setDescription(text, meta.join(QStringLiteral("  ·  ")));
        // "144, 240, 360, ...": the heights of every format the video has.
        int tallest = 0;
        for (const QString &h : facts.value(3).split(QLatin1Char(',')))
            tallest = qMax(tallest, h.trimmed().toInt());
        m_videoPage->setAvailableHeight(tallest);
        m_videoChannelId = facts.value(2).trimmed().startsWith(QLatin1String("UC")) ? facts.value(2).trimmed() : QString();
        m_videoPage->setChannel(m_videoChannelId, follows(m_videoChannelId));
        // Its name, when mpv had none for it (a live stream has no uploader).
        const QString name = facts.mid(4).join(QLatin1Char('|')).trimmed();
        if (!name.isEmpty() && name != QLatin1String("NA"))
            m_videoPage->setChannelName(name);
        showVideoChannel();
    };
    if (m_descriptions.contains(id)) {
        if (!m_preview || m_previewTab.id == id)
            show(m_descriptions.value(id));
        return;
    }
    if (m_describing == id)
        return;
    m_describing = id;
    auto *proc = new QProcess(this);
    // Python's own output encoding, so Turkish letters arrive as they are.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    proc->setProcessEnvironment(env);
    connect(proc, &QProcess::finished, this, [this, proc, id, show] {
        proc->deleteLater();
        const QString text = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        m_descriptions.insert(id, text);
        if (m_describing == id)
            m_describing.clear();
        const QString now = youtube::videoId(m_mpv.property("user-data/ghost/source").toString().isEmpty()
                                                 ? m_mpv.property("path").toString()
                                                 : m_mpv.property("user-data/ghost/source").toString());
        if (m_preview ? m_previewTab.id == id : now == id)
            show(text);
    });
    proc->start(QFileInfo(findMpv()).absoluteDir().filePath("yt-dlp.exe"),
                {"--skip-download", "--no-playlist", "--no-warnings", "--encoding", "utf-8",
                 // First line: the first formats, where the storyboards are.
                 "--print", "%(formats.0:6)j",
                 "--print", "%(view_count)s|%(upload_date)s|%(channel_id)s|%(formats.:.height)l|%(channel)s", "--print", "description",
                 QStringLiteral("https://www.youtube.com/watch?v=") + id});
}

void MainWindow::showSongInfo(const QString &target, const QString &title, const QString &artist)
{
    // Its own video if the entry names one, the one matched for its search
    // otherwise, or YouTube's first answer to the search.
    auto *dialog = new SongInfoDialog(title, artist, this);
    QPointer<SongInfoDialog> alive(dialog);
    const QString known = idForTarget(target);
    auto cover = [this, alive](const QString &id) {
        if (!alive || id.isEmpty())
            return;
        alive->setProperty("videoId", id);
        alive->setCover(m_covers.get(id, ThumbnailCache::Size::Card));
    };
    cover(known);
    // A picture not downloaded yet comes when it is.
    connect(&m_covers, &ThumbnailCache::ready, dialog, [this, alive](const QString &id) {
        if (alive && id == alive->property("videoId").toString())
            alive->setCover(m_covers.get(id, ThumbnailCache::Size::Card));
    });
    dialog->show();
    auto fill = [this, alive, known, cover](const QString &printed) {
        if (!alive)
            return;
        const songinfo::Facts facts = songinfo::parse(printed);
        alive->setFacts(facts);
        if (known.isEmpty())
            cover(facts.id);
    };
    const QString query = youtube::searchQuery(target);
    const QString url = !known.isEmpty() ? QStringLiteral("https://www.youtube.com/watch?v=") + known
                        : !query.isEmpty() ? QStringLiteral("ytsearch1:") + query
                                           : target;
    fetchSongInfo(known.isEmpty() ? target : known, url, [fill, alive](const QString &printed) {
        if (printed.isEmpty()) {
            if (alive)
                alive->setFailed();
            return;
        }
        fill(printed);
    });
}

void MainWindow::fetchSongInfo(const QString &key, const QString &url, std::function<void(const QString &)> done)
{
    // One yt-dlp call per song and session; what it printed is kept, empty
    // when it failed (and then asked again next time).
    if (m_songInfo.contains(key)) {
        done(m_songInfo.value(key));
        return;
    }
    auto *proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    proc->setProcessEnvironment(env);
    connect(proc, &QProcess::finished, this, [this, proc, key, done] {
        proc->deleteLater();
        const QString printed = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        if (!printed.isEmpty())
            m_songInfo.insert(key, printed);
        done(printed);
    });
    proc->start(QFileInfo(findMpv()).absoluteDir().filePath("yt-dlp.exe"), songinfo::ytDlpArguments(url));
}

// -- the now playing view ----------------------------------------------------------

void MainWindow::updatePanelVisibility()
{
    if (!m_panel)
        return;
    const bool playing = m_mpv.connected() && !m_mpv.property("path").toString().isEmpty();
    // Not over a video: its page has the list at its side already.
    const bool was = m_panel->isVisible();
    // The music side's: on the YouTube side the island leads back to what
    // plays (Batin, 2026-11-20).
    m_panel->setVisible(m_panelOpen && playing && !m_youtubeLayout && m_pages->currentWidget() != m_videoPage && !m_videoFull && panelFits());
    m_nowPlaying->setPanelOpen(m_panelOpen);
    if (m_panel->isVisible() && !was)
        m_panelSoon.start();   // filled again once it is back
}

bool MainWindow::panelFits() const
{
    // In a window too narrow for the page beside it the view is left out
    // (still open, it comes back as the window widens): squeezed in, its
    // right side and its close button were past the window's edge.
    const qreal z = theme::zoom();
    const int library = m_libraryWanted >= 0 ? m_libraryWanted : m_split->sizes().value(0);
    return width() - 32 - library - qRound(340 * z) >= qRound(380 * z);
}

void MainWindow::refreshPanel()
{
    // "Loading the video..." ends when the video is there, even if the word
    // that it came was missed.
    if (m_findingVideo && songVideo() && !panelVideo())
        m_findingVideo = false;
    if (m_findingMini && panelVideo())
        m_findingMini = false;
    // A video list (a YouTuber, not a song): no lyrics, a channel.
    const bool videoPlaying = m_mpv.connected() && !m_mpv.property("path").toString().isEmpty() &&
                              !m_mpv.property("user-data/ghost/audio").toBool();
    m_nowPlaying->setLyricsShown(!videoPlaying);
    m_panel->setChannelMode(videoPlaying);
    updatePanelVisibility();
    if (!m_panel->isVisible())
        return;
    const QString path = m_mpv.property("path").toString();
    const QString list = playingListPath();
    m_panel->setList(list.isEmpty() ? QString() : Playlist::peek(list).name);
    const auto [artist, track] = playingSong();
    m_panel->setSong(track.isEmpty() ? m_mpv.property("media-title").toString() : track, artist);
    QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString());
    if (id.isEmpty())
        id = idForTarget(path);
    m_panel->setCover(m_covers.get(id, ThumbnailCache::Size::Hero));
    const bool mini = panelVideo();
    m_panel->setVideoState(m_mpv.property("user-data/ghost/audio").toBool(), songVideo() && !mini, m_findingVideo,
                           mini, m_findingMini);
    // The picture's shape while the video plays in the cover's place.
    const double aspect = m_mpv.property("video-params/aspect").toDouble();
    m_panel->setCoverAspect(mini || m_findingMini ? (aspect > 0.5 && aspect < 2.6 ? aspect : 16.0 / 9.0) : 1.0);
    m_panel->setPosition(m_mpv.property("time-pos").toDouble());

    // Credits and the artist, once per video.
    if (!id.isEmpty() && id != m_panelFacts) {
        m_panelFacts = id;
        m_panelChannel.clear();
        m_panelChannelId.clear();
        updateFollowButton();
        m_panel->setCredits({}, false);
        m_panel->setArtist(nullptr);
        fetchSongInfo(id, QStringLiteral("https://www.youtube.com/watch?v=") + id, [this, id](const QString &printed) {
            if (id != m_panelFacts)
                return;
            const songinfo::Facts facts = songinfo::parse(printed);
            m_panel->setCredits(facts.credits, true);
            // A YouTube video (not a song) is about its channel, as the card
            // says: MusicCat's "Lady Gaga - Paparazzi (Lyrics)" showed Lady
            // Gaga under "About the channel" (Batin, 2026-11-20). A song,
            // or a song's video, keeps its artist.
            const bool youtubeVideo = !m_mpv.property("user-data/ghost/audio").toBool();
            const QString artist = youtubeVideo ? QString() : playingSong().first;
            m_panelChannelId = facts.channelId;
            m_panelChannelName = artist.isEmpty() ? m_mpv.property("metadata/by-key/uploader").toString() : artist;
            updateFollowButton();
            m_panelChannel = ArtistLookup::keyFor(artist.isEmpty() ? facts.channelId : artist);
            m_panel->setArtist(m_artists.known(m_panelChannel));
            m_artists.fetch(artist, facts.channelId);
        });
    }

    // What plays next: the next entry of mpv's list, with the name the list
    // gives it.
    const QVariantList entries = m_mpv.property("playlist").toList();
    const int pos = m_mpv.property("playlist-pos").toInt();
    if (pos >= 0 && pos + 1 < entries.size()) {
        const QVariantMap next = entries[pos + 1].toMap();
        const QString target = next.value("filename").toString();
        QString nextTitle = next.value("title").toString();
        if (nextTitle.isEmpty())
            nextTitle = m_queueTitles.value(target, m_autoplayTitles.value(target, m_suggestedTitles.value(target)));   // queue, Autoplay, a lone video's suggestions
        auto [nextTrack, nextArtist] = splitTitle(nextTitle);
        if (nextTrack.isEmpty())
            nextTrack = target;
        m_panel->setNext(nextTrack, nextArtist, m_covers.get(idForTarget(target), ThumbnailCache::Size::Card));
    } else {
        m_panel->setNext(QString(), QString(), QPixmap());
    }
}

bool MainWindow::songVideo() const
{
    return m_mpv.connected() && m_mpv.property("user-data/ghost/video-on").toBool();
}

bool MainWindow::panelVideo() const
{
    return songVideo() && m_mpv.property("user-data/ghost/video-panel").toBool();
}

void MainWindow::setPanelVideo(bool on)
{
    // ghost.lua plays the entry named in video-once as its video and forgets
    // it at the next song. Starts again at the same second, as the switch.
    if (!m_mpv.connected() || on == panelVideo())
        return;
    if (on) {
        QString format = mpvconf::value("ytdl-format");
        if (format.isEmpty())
            format = QStringLiteral("bestvideo[height<=?1080]+bestaudio/best");
        m_mpv.setProperty("user-data/ghost/video-format", format);
        m_findingMini = true;
        m_miniPath = m_mpv.property("path").toString();
        // Not found in time, or another song came first: the button is the
        // button again rather than "Loading the video..." for good.
        QTimer::singleShot(30000, this, [this, path = m_miniPath] {
            if (m_findingMini && m_miniPath == path) {
                m_findingMini = false;
                refreshPanel();
            }
        });
    }
    // ghost.lua names the entry from mpv's own path and replays it at the
    // same second.
    armStartReset(m_mpv.property("playlist-pos").toInt());
    m_mpv.command({"script-message", "ghost-video-once", on ? "yes" : "no"});
    refreshPanel();
    schedulePlace();
}

void MainWindow::setSongVideo(bool on)
{
    // The whole list turns to videos from this song on, until "Switch to
    // audio" or another list: ghost.lua plays each song as its video,
    // songmatch.lua finds it on YouTube. This song starts again at the same
    // second.
    if (!m_mpv.connected() || on == (songVideo() && !panelVideo()))
        return;
    if (on && panelVideo()) {
        // Already a video in the view: the list goes on as videos from here,
        // on the video page, with nothing to load again.
        m_mpv.setProperty("user-data/ghost/video-mode", true);
        m_mpv.setProperty("user-data/ghost/video-panel", false);
        m_mpv.setProperty("user-data/ghost/video-once", QString());   // or "Switch to audio" would keep it
        showVideoPage();
        refreshPanel();
        return;
    }
    if (on) {
        QString format = mpvconf::value("ytdl-format");
        if (format.isEmpty())
            format = QStringLiteral("bestvideo[height<=?1080]+bestaudio/best");
        m_mpv.setProperty("user-data/ghost/video-format", format);
        m_findingVideo = true;
        m_wantVideoPage = true;
    }
    m_mpv.setProperty("user-data/ghost/video-mode", on);
    m_mpv.setProperty("user-data/ghost/video-once", QString());
    m_mpv.setProperty("start", QString::number(m_mpv.property("time-pos").toDouble(), 'f', 1));
    armStartReset(m_mpv.property("playlist-pos").toInt());
    m_mpv.command({"playlist-play-index", m_mpv.property("playlist-pos").toInt()});
    refreshPanel();
}

void MainWindow::songVideoChanged()
{
    schedulePlace();
    m_findingVideo = false;
    m_findingMini = false;
    m_videoPage->setSongVideo(songVideo() && !panelVideo());
    if (songVideo()) {
        // Inside the app the video opens on its page; in mpv's own window,
        // there.
        if (m_wantVideoPage && m_mpvEmbedded)
            showVideoPage();
        m_wantVideoPage = false;
        return;
    }
    // Back to songs: off the video page.
    if (m_pages->currentWidget() == m_videoPage && m_mpv.property("user-data/ghost/audio").toBool()) {
        setVideoFullScreen(false);
        QWidget *back = m_beforeVideo && m_beforeVideo != m_videoPage ? m_beforeVideo.data() : nullptr;
        if (back)
            m_pages->setCurrentWidget(back);
        else
            showHome();
    }
}

void MainWindow::setVideoFullScreen(bool full)
{
    // Only the picture, the whole screen; Esc or F comes back.
    if (full == m_videoFull)
        return;
    m_videoFull = full;
    startSurfaceDrag();   // the frame on screen now, before anything moves
    // The library's width is kept: hidden, the panel reads as narrowed and
    // would come back in its covers-only form.
    if (full) {
        m_libraryWidthBeforeFull = m_split->sizes().value(0);
        m_compactBeforeFull = m_compact;
    }
    m_topBar->setVisible(!full);
    updatePanelVisibility();
    m_split->widget(0)->setVisible(!full);
    schedulePlace();
    // No gap around the picture either.
    if (auto *body = centralWidget()->layout()->itemAt(1)->layout())
        body->setContentsMargins(full ? 0 : 8, 0, full ? 0 : 8, 0);
    m_videoPage->setFullScreen(full);
    if (!full && m_fullFromPanel) {
        m_fullFromPanel = false;
        // It was watched small (the corner player, or the now playing
        // view): small again. The way through the video page had reset this,
        // and the picture went away (2026-11-11).
        m_miniAsked = true;
        m_miniClosed = false;
        m_pictureAway = false;
        QWidget *back = m_beforeVideo && m_beforeVideo != m_videoPage ? m_beforeVideo.data() : nullptr;
        if (back)
            m_pages->setCurrentWidget(back);
        else
            showHome();
    }
    if (!full)
        // Once the window has settled back into its size: each resize on the
        // way out of full screen squeezed the panel again. Put back on every
        // resize for a moment, and once more after it.
        m_restoreLibraryUntil.start();
        QTimer::singleShot(150, this, &MainWindow::restoreLibraryWidth);
        QTimer::singleShot(600, this, &MainWindow::restoreLibraryWidth);
    // Full screen, Windows 11's round corners and 1 px border would show the
    // desktop at the screen's edges.
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const int corners = full ? 1 : 2;   // DWMWA_WINDOW_CORNER_PREFERENCE: none, round
    DwmSetWindowAttribute(hwnd, 33, &corners, sizeof(corners));
    const COLORREF border = full ? 0xFFFFFFFE : 0xFFFFFFFF;   // DWMWA_BORDER_COLOR: none, the default
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
    if (full) {
        m_wasMaximized = isMaximized();
        if (m_wasMaximized) {
            // On the way out Qt puts the window at its normal size and only
            // then maximises it: two frames of a small window with the
            // desktop around it (filmed). Its normal size is made the whole
            // work area meanwhile, and the real one put back after.
            WINDOWPLACEMENT wp{sizeof(wp)};
            MONITORINFO screen{sizeof(screen)}, primary{sizeof(primary)};
            if (GetWindowPlacement(hwnd, &wp) &&
                GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &screen) &&
                GetMonitorInfo(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &primary)) {
                m_normalPlace = QRect(QPoint(wp.rcNormalPosition.left, wp.rcNormalPosition.top),
                                      QPoint(wp.rcNormalPosition.right - 1, wp.rcNormalPosition.bottom - 1));
                // Placements are in work area coordinates (the primary's).
                OffsetRect(&screen.rcWork, -primary.rcWork.left, -primary.rcWork.top);
                wp.rcNormalPosition = screen.rcWork;
                SetWindowPlacement(hwnd, &wp);
            }
        }
        showFullScreen();
        m_videoPage->setFocus();
    } else if (m_wasMaximized) {
        // Straight back to maximised: through showNormal() the window stood
        // two frames at its normal size, the desktop showing around it
        // (filmed).
        setWindowState(Qt::WindowMaximized);
        WINDOWPLACEMENT wp{sizeof(wp)};
        if (m_normalPlace.isValid() && GetWindowPlacement(hwnd, &wp)) {
            wp.rcNormalPosition = {m_normalPlace.left(), m_normalPlace.top(), m_normalPlace.right() + 1, m_normalPlace.bottom() + 1};
            SetWindowPlacement(hwnd, &wp);   // still maximised: nothing moves
        }
        m_normalPlace = QRect();
    } else {
        showNormal();
    }
}

void MainWindow::closeExtraPlayers(bool all)
{
    // mpv processes this app started that are not the one it talks to (or,
    // closing with no window, every one of them): ended, so none plays on
    // where nobody can see or stop it. Each is checked to still be mpv.exe,
    // since Windows reuses process ids.
    const qint64 ours = m_mpv.property("pid").toLongLong();
    if (!all && ours <= 0) {
        // mpv has not said which process it is yet (on a first start it can
        // be busy opening the first song for seconds): the one just started
        // was taken for a stray and ended, and the first Play did nothing.
        if (m_mpv.connected())
            QTimer::singleShot(1500, this, [this] { closeExtraPlayers(false); });
        return;
    }
    QList<qint64> keep;
    for (qint64 pid : std::as_const(m_launchedPids)) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
        if (!h)
            continue;   // gone already
        wchar_t name[MAX_PATH];
        DWORD size = MAX_PATH;
        DWORD code = 0;
        const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
        const bool isMpv = QueryFullProcessImageNameW(h, 0, name, &size) &&
                           QString::fromWCharArray(name, int(size)).endsWith(QStringLiteral("mpv.exe"), Qt::CaseInsensitive);
        if (alive && isMpv) {
            if (all || pid != ours) {
                playLog(QStringLiteral("closing mpv %1 (talking to %2)").arg(pid).arg(ours));
                TerminateProcess(h, 0);
            }
            else
                keep << pid;
        }
        CloseHandle(h);
    }
    m_launchedPids = keep;
}

void MainWindow::rememberPlayed(const QString &path)
{
    // What was played from a search is not a list of the library.
    if (QDir::cleanPath(QFileInfo(path).absolutePath()).compare(foundFolder(), Qt::CaseInsensitive) == 0)
        return;
    QSettings settings;
    QStringList played = settings.value("recent/played").toStringList();
    played.removeAll(path);
    played.prepend(path);
    settings.setValue("recent/played", played.mid(0, 20));

    // The list just played goes to the top of the library, as Spotify's
    // Recents does, and its folder to the top with it; pinned ones stay
    // above. The level's whole order is written, so a list never placed
    // (just added) no longer sits above it.
    const QString key = QFileInfo(path).fileName();
    QString folder;
    for (const Playlist &p : m_library.playlists())
        if (QFileInfo(p.path).fileName() == key)
            folder = p.folder;
    const auto toFront = [this, &settings](const QString &level, const QString &front) {
        QStringList keys;
        if (level.isEmpty()) {
            for (const QString &f : m_library.subfolders())
                keys << QStringLiteral("folder:") + f;
            for (const Playlist &p : m_library.playlists())
                if (p.folder.isEmpty())
                    keys << QFileInfo(p.path).fileName();
        } else {
            for (const Playlist &p : m_library.playlists())
                if (p.folder == level)
                    keys << QFileInfo(p.path).fileName();
        }
        if (!keys.contains(front))
            return;
        const QStringList pinned = settings.value("library/pinned").toStringList();
        QStringList order;
        for (const QString &k : arranged(keys, level))
            if (!pinned.contains(k) && k != front)
                order << k;
        order.prepend(front);
        settings.setValue("library/order/" + (level.isEmpty() ? QStringLiteral("_top") : level), order);
    };
    toFront(folder, key);
    if (!folder.isEmpty())
        toFront(QString(), QStringLiteral("folder:") + folder);
    QTimer::singleShot(0, this, &MainWindow::refill);
}

void MainWindow::importFiles(const QStringList &paths)
{
    QString last;
    for (const QString &path : paths) {
        if (!path.endsWith(".m3u", Qt::CaseInsensitive) && !path.endsWith(".m3u8", Qt::CaseInsensitive))
            continue;
        const QString moved = m_library.importFile(path);
        if (!moved.isEmpty())
            last = moved;
    }
    if (last.isEmpty())
        return;
    m_library.rescan();
    refill();
    const auto &all = m_library.playlists();
    for (int i = 0; i < all.size(); ++i)
        if (all[i].path.compare(last, Qt::CaseInsensitive) == 0)
            openPlaylist(i);
    bringToFront();
}

void MainWindow::bringToFront()
{
    // Back as it was left: from the tray, from the taskbar, maximised or not.
    // showNormal() would also take a maximised window down to its normal size.
    show();
    if (isMinimized())
        ShowWindow(reinterpret_cast<HWND>(winId()), SW_RESTORE);
    raise();
    activateWindow();
}

void MainWindow::showMinimizedAtStartup()
{
    // Left maximised, opened minimised at sign-in: Qt called the window
    // maximised but never told Windows, which brought it back from the
    // taskbar at its normal size. The two disagreed from then on: a gap at
    // the screen's edge, no snapping to the top, FancyZones not seeing it.
    const bool maximized = isMaximized();
    showMinimized();
    HWND hwnd = reinterpret_cast<HWND>(winId());
    WINDOWPLACEMENT wp{sizeof(wp)};
    if (maximized && GetWindowPlacement(hwnd, &wp)) {
        wp.flags |= WPF_RESTORETOMAXIMIZED;
        wp.showCmd = SW_SHOWMINNOACTIVE;
        SetWindowPlacement(hwnd, &wp);
    }
    keepFrameStyle();
}

void MainWindow::keepFrameStyle()
{
    // The frame bits make Windows treat the frameless window as a window:
    // snapping, maximising by dragging to the top, FancyZones. Full screen
    // goes without them on purpose.
    if (windowState() & Qt::WindowFullScreen)
        return;
    HWND hwnd = reinterpret_cast<HWND>(winId());
    const LONG want = WS_THICKFRAME | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if ((style & want) != want) {
        SetWindowLongW(hwnd, GWL_STYLE, style | want);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void MainWindow::addFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, "Add a folder with playlists");
    if (folder.isEmpty())
        return;
    m_library.addFolder(folder);
    rescan();
}

QString MainWindow::foundFolder()
{
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/search"));
}

void MainWindow::runSearch()
{
    const QString text = m_search->text().trimmed();
    if (text.isEmpty() && m_searchPage->youTubeMode() && m_pages->currentWidget() == m_searchPage) {
        // Emptied in YouTube mode: its home again.
        m_searchPage->setQuery(QString());
        m_searchPage->showYouTubeHome();
        markYouTubeMenu(0);
        return;
    }
    if (text.isEmpty()) {
        // Emptied: back where the search started from.
        if (m_pages->currentWidget() == m_searchPage)
            m_pages->setCurrentWidget(m_beforeSearch ? m_beforeSearch : m_pages->widget(0));
        m_searchPage->setQuery(QString());
        return;
    }
    if (m_pages->currentWidget() != m_searchPage) {
        QWidget *now = m_pages->currentWidget();
        // The video page keeps its own way back; the search returns home then.
        m_beforeSearch = now == m_videoPage || now == m_lyricsView ? m_pages->widget(0) : now;
        if (now == m_lyricsView)
            m_beforeLyrics = nullptr;
    }
    m_searchPage->setQuery(text);
    m_pages->setCurrentWidget(m_searchPage);
    SearchPopup::remember(text);
    markYouTubeMenu(kYtSearch);
}

void MainWindow::openSearch()
{
    // The middle box when that is chosen; else the top one.
    if (!QSettings().value("search/middle", false).toBool()) {
        m_search->setFocus();
        m_search->selectAll();
        return;
    }
    openSearchPopup();
}

void MainWindow::openSearchPopup()
{
    if (!m_searchPopup) {
        m_searchPopup = new SearchPopup(this);
        m_searchPopup->onSearch = [this](const QString &text) {
            {
                const QSignalBlocker quiet(m_search);
                m_search->setText(text);
            }
            m_searchTimer.stop();
            runSearch();
        };
        // The video's controls and click catcher are windows of their own
        // over the picture, raised as the mouse moves there; while the box
        // is open they would come up over it, so they wait until it closes.
        m_searchPopup->onShown = [this](bool shown) {
            if (shown) {
                m_videoPage->controls()->hide();
                m_videoPage->corners()->hide();
            } else {
                placeSurface();
            }
        };
    }
    m_searchPopup->open(m_search->text(), m_youtubeLayout ? QStringLiteral("Search YouTube")
                                                          : QStringLiteral("What do you want to play?"));
}

void MainWindow::setSearchMiddle(bool middle)
{
    m_search->setVisible(!middle);
    m_searchButton->setVisible(middle);
}

void MainWindow::playFound(const QVector<ytmusic::Item> &items, const QString &name, int start, bool shuffle, bool audio)
{
    // A list file of its own in the app's cache, as the bot writes them, so
    // mpv, the scripts and "pick up where you left off" treat it as any
    // other. A file per list: one being played is never rewritten under it.
    const QString dir = foundFolder();
    QDir().mkpath(dir);
    QByteArray body = "#EXTM3U\r\n";
    if (audio)
        body += "#GHOST_AUDIO\r\n";
    body += QStringLiteral("#PLAYLIST:%1\r\n").arg(name).toUtf8();
    for (const ytmusic::Item &it : items)
        body += QStringLiteral("#EXTINF:-1, %1\r\n%2\r\n").arg(it.lineTitle(), it.target()).toUtf8();
    const QString path = dir + '/' + QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Sha1).toHex().left(16)) + QStringLiteral(".m3u");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size()) {
        toast("Couldn't start it: the search list couldn't be written.");
        return;
    }
    file.close();
    // The newest 30 are kept (one may be what plays, or what resumes later).
    QFileInfoList old = QDir(dir).entryInfoList({"*.m3u"}, QDir::Files, QDir::Time);
    for (int i = 30; i < old.size(); ++i)
        QFile::remove(old[i].absoluteFilePath());
    play(path, shuffle, qBound(0, start, int(items.size()) - 1));
}

void MainWindow::saveFound(const QString &name, const QVector<ytmusic::Item> &items)
{
    // A radio kept from a search is marked so: it wears the radio page's
    // ghost cover (drawn, not a picture file) until Change cover... gives it
    // one, and keeps it renamed (Batın, 2026-11-11).
    const bool radio = name.startsWith(QStringLiteral("Ghost Engine Radio #"));
    const QString path = m_library.createPlaylist(name, QString(), radio);
    if (path.isEmpty()) {
        QMessageBox::warning(this, "Save to library", "The playlist couldn't be made.");
        return;
    }
    QVector<PlaylistEntry> entries;
    for (const ytmusic::Item &it : items)
        entries << PlaylistEntry{it.lineTitle(), it.target(), {}};
    if (!Playlist::append(path, entries)) {
        QMessageBox::warning(this, "Save to library", "The playlist file couldn't be written.");
        return;
    }
    toast(QStringLiteral("Saved to your library: %1").arg(name));
    m_searchPage->markSaved();
    reopenAfter(path);
}

void MainWindow::foundMenu(const QVector<ytmusic::Item> &items, const QPoint &global)
{
    if (items.isEmpty())
        return;
    const bool videos = items.first().kind == ytmusic::Kind::Video;
    QMenu menu(this);
    menu.addAction(items.size() > 1 ? QStringLiteral("Play these %1").arg(items.size()) : QStringLiteral("Play"), this,
                   [this, items, videos] { playFound(items, items.size() > 1 ? QStringLiteral("Search: %1").arg(m_searchPage->query()) : items.first().title, 0, false, !videos); });
    if (items.size() == 1 && videos && !items.first().id.isEmpty()) {
        const ytmusic::Item it = items.first();
        menu.addAction("Open in new tab", this, [this, it] {
            openInTab(VideoTab{it.target(), it.id, it.title, it.artist, QString(), 0});
        });
    }
    if (items.size() == 1 && !videos)
        menu.addAction("Play with its radio", this, [this, items] { m_searchPage->playRadio(items.first()); });
    if (items.size() == 1 && !videos && !items.first().artist.isEmpty())
        menu.addAction("Go to artist", this, [this, artist = items.first().artist] { goToArtist(artist); });
    {
        // Videos too (Batin, 2026-11-20): after what plays, as a song.
        QVector<PlaylistEntry> picked;
        for (const ytmusic::Item &it : items)
            picked << PlaylistEntry{it.lineTitle(), it.target(), {}};
        menu.addAction("Add to queue", this, [this, picked] { queueSongs(picked, false); });
        menu.addAction("Play next", this, [this, picked] { queueSongs(picked, true); });
    }
    if (items.size() == 1 && !items.first().id.isEmpty() && items.first().kind != ytmusic::Kind::Playlist) {
        const QString id = items.first().id;
        menu.addAction(videos ? "Copy video link" : "Copy song link", this, [this, id, videos] { copyLink(id, !videos); });
    }
    if (videos) {
        bool allKept = true;
        QSet<QString> kept;
        for (const ytmusic::Item &it : watchLater())
            kept.insert(it.id);
        for (const ytmusic::Item &it : items)
            allKept = allKept && kept.contains(it.id);
        menu.addAction(allKept ? QStringLiteral("Remove from Watch later") : QStringLiteral("Save to Watch later"), this,
                       [this, items, allKept] { setWatchLater(items, !allKept); });
    }
    if (items.size() == 1 && videos && !items.first().channelId.isEmpty()) {
        const ytmusic::Item it = items.first();
        menu.addAction(QStringLiteral("Go to %1").arg(it.artist.isEmpty() ? QStringLiteral("the channel") : it.artist), this,
                       [this, it] {
            showYouTube();
            markYouTubeMenu(-1, -1, it.channelId);
            m_searchPage->showChannel(it.channelId, it.artist);
        });
    }
    if (items.size() == 1 && videos && !items.first().channelId.isEmpty()) {
        const ytmusic::Item it = items.first();
        const bool on = follows(it.channelId);
        menu.addAction(on ? QStringLiteral("Unfollow %1").arg(it.artist) : QStringLiteral("Follow %1").arg(it.artist), this,
                       [this, it, on] { setFollowing(it.channelId, it.artist, !on); });
        if (!on)
            menu.addAction(QStringLiteral("Show less from %1").arg(it.artist), this,
                           [this, it] { m_searchPage->showLess(it.channelId); });
    }
    QMenu *to = menu.addMenu(items.size() > 1 ? QStringLiteral("Add %1 songs to playlist").arg(items.size()) : QStringLiteral("Add to playlist"));
    QVector<PlaylistEntry> entries;
    for (const ytmusic::Item &it : items)
        entries << PlaylistEntry{it.lineTitle(), it.target(), {}};
    fillAddTo(to, entries, QString(), videos);
    menu.exec(global);
}


// "New playlist...", then every list of the library these can be added to.
void MainWindow::openInTab(const VideoTab &tab)
{
    // Kept, not opened: the tab waits with its name and cover. What plays
    // becomes a tab beside it, so the two can be gone between.
    if (tab.target.isEmpty())
        return;
    syncTabs();
    for (int i = 0; i < m_tabStrip->tabs.size(); ++i)
        if (m_tabStrip->tabs[i].target == tab.target) {
            toast(i == m_tabStrip->current ? QStringLiteral("That's what plays") : QStringLiteral("Already in a tab"));
            return;
        }
    const bool video = m_mpv.connected() && !m_mpv.property("path").toString().isEmpty() &&
                       !m_mpv.property("user-data/ghost/audio").toBool();
    if (m_tabStrip->tabs.isEmpty() && video) {
        m_tabStrip->tabs << VideoTab{};
        m_tabStrip->current = 0;
        syncTabs();   // fills it from what plays
    }
    m_tabStrip->tabs << tab;
    tabsChanged();
    toast(QStringLiteral("Opened in a new tab"));
}

void MainWindow::pickTab(int index)
{
    if (index < 0 || index >= m_tabStrip->tabs.size())
        return;
    if (index == m_tabStrip->current) {
        // Already what plays: its page.
        endPreview();
        if (!m_youtubeLayout)
            setYouTubeLayout(true, true, [this] { showVideoPage(); });
        else
            showVideoPage();
        return;
    }
    // Instant Play off (the default): only its page; what plays goes on
    // until Play there (Batin, 2026-11-20).
    if (!QSettings().value("video/instantPlay", false).toBool()) {
        if (!m_youtubeLayout)
            setYouTubeLayout(true, true, [this, index] { startPreview(index); });
        else
            startPreview(index);
        return;
    }
    endPreview(false);
    // The one left keeps the second it was at (time-pos keeps it); the one
    // picked opens at its own, from its list when it came from one.
    const VideoTab tab = m_tabStrip->tabs[index];
    m_tabStrip->current = index;
    tabsChanged();
    // Until mpv has it, what still plays is the one left, not this tab's.
    m_tabPending = tab.target;
    QTimer::singleShot(20000, this, [this, t = tab.target] { if (m_tabPending == t) m_tabPending.clear(); });
    m_startSeconds = tab.seconds > 5 ? tab.seconds - 2 : 0;   // a little before, as YouTube resumes
    if (!tab.list.isEmpty() && QFileInfo::exists(tab.list)) {
        const QVector<PlaylistEntry> entries = Playlist::peek(tab.list).readEntries();
        for (int i = 0; i < entries.size(); ++i)
            if (entries[i].target == tab.target) {
                play(tab.list, false, i);
                return;
            }
    }
    ytmusic::Item item;
    item.kind = ytmusic::Kind::Video;
    item.id = tab.id.isEmpty() ? youtube::videoId(tab.target) : tab.id;
    item.title = tab.title;
    item.artist = tab.channel;
    if (item.id.isEmpty()) {
        m_startSeconds = 0;
        toast(QStringLiteral("This tab's video couldn't be found."));
        return;
    }
    playFound({item}, tab.title, 0, false, false);
}

void MainWindow::closeTab(int index)
{
    if (index < 0 || index >= m_tabStrip->tabs.size())
        return;
    const bool playing = index == m_tabStrip->current;
    if (index == m_tabStrip->viewing)
        endPreview();
    else if (m_tabStrip->viewing > index)
        --m_tabStrip->viewing;
    m_tabStrip->tabs.removeAt(index);
    if (m_tabStrip->current > index)
        --m_tabStrip->current;
    else if (playing)
        m_tabStrip->current = -1;
    // What plays closed while others wait: the next one opens, as a
    // browser's does. The last one left plays on, without a strip.
    if (playing && !m_tabStrip->tabs.isEmpty()) {
        pickTab(qMin(index, int(m_tabStrip->tabs.size()) - 1));
        return;
    }
    tabsChanged();
}

void MainWindow::syncTabs()
{
    if (!m_tabStrip || m_tabStrip->tabs.isEmpty() || !m_mpv.connected())
        return;
    const QString path = m_mpv.property("path").toString();
    if (path.isEmpty())
        return;
    if (!m_tabPending.isEmpty()) {
        if (path != m_tabPending)
            return;
        m_tabPending.clear();
    }
    QVector<VideoTab> &tabs = m_tabStrip->tabs;
    int &current = m_tabStrip->current;
    // A song is not a tab's: the tab that played is left where it was.
    if (m_mpv.property("user-data/ghost/audio").toBool()) {
        if (current >= 0) {
            current = -1;
            tabsChanged();
        }
        return;
    }
    if (current < 0) {
        // A video opened another way while tabs wait: it is a tab of its own,
        // unless one is this very video.
        for (int i = 0; i < tabs.size() && current < 0; ++i)
            if (tabs[i].target == path)
                current = i;
        if (current < 0) {
            tabs << VideoTab{};
            current = int(tabs.size()) - 1;
        }
    }
    VideoTab &tab = tabs[current];
    if (tab.target != path) {
        // Gone on in its tab (the next of its list, a pick from the right):
        // the tab is that one now, from its start.
        tab.target = path;
        tab.seconds = 0;
        tab.list = playingListPath();
        tab.title.clear();
        tab.channel.clear();
        tab.id.clear();
    }
    if (tab.list.isEmpty())
        tab.list = playingListPath();
    QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString());
    if (id.isEmpty())
        id = youtube::videoId(path);
    if (!id.isEmpty())
        tab.id = id;
    const QString title = m_mpv.property("media-title").toString();
    if (!title.isEmpty() && !title.startsWith(QStringLiteral("http")) && !title.startsWith(QStringLiteral("ytdl")))
        tab.title = title;
    if (tab.title.isEmpty())
        tab.title = m_suggestedTitles.value(path, m_queueTitles.value(path));
    const QString uploader = m_mpv.property("metadata/by-key/uploader").toString();
    if (!uploader.isEmpty())
        tab.channel = uploader;
    tabsChanged();
}

void MainWindow::tabsChanged()
{
    if (!m_tabStrip)
        return;
    m_tabStrip->changed();
    // Shown on the YouTube side while a tab waits beside what plays.
    bool waiting = false;
    for (int i = 0; i < m_tabStrip->tabs.size(); ++i)
        waiting = waiting || i != m_tabStrip->current;
    const bool show = m_youtubeLayout && waiting;
    if (show != m_tabStrip->isVisible()) {
        m_tabStrip->setVisible(show);
        if (m_island)
            QTimer::singleShot(0, m_island, [this] { m_island->refresh(); });
    }
    fitTabs();
    // Kept for the next start; the one that plays too, at its second.
    QJsonArray saved;
    if (waiting && QSettings().value("youtube/rememberTabs", true).toBool())
        for (const VideoTab &t : std::as_const(m_tabStrip->tabs))
            saved.append(QJsonObject{{"target", t.target}, {"id", t.id}, {"title", t.title},
                                     {"channel", t.channel}, {"list", t.list}, {"seconds", qRound(t.seconds)}});
    QSettings().setValue("youtube/tabs", QJsonDocument(saved).toJson(QJsonDocument::Compact));
}

void MainWindow::tabMenu(int index, const QPoint &global)
{
    if (index < 0 || index >= m_tabStrip->tabs.size())
        return;
    const VideoTab tab = m_tabStrip->tabs[index];
    QMenu menu(this);
    menu.addAction(index == m_tabStrip->current ? QStringLiteral("Go to video") : QStringLiteral("Play"), this,
                   [this, index] { pickTab(index); });
    if (!tab.id.isEmpty())
        menu.addAction("Copy video link", this, [this, id = tab.id] { copyLink(id, false); });
    menu.addSeparator();
    menu.addAction("Close tab", this, [this, index] { closeTab(index); });
    if (m_tabStrip->tabs.size() > 1)
        menu.addAction("Close other tabs", this, [this, index] {
            const VideoTab keep = m_tabStrip->tabs[index];
            const bool wasPlaying = index == m_tabStrip->current;
            m_tabStrip->tabs = {keep};
            m_tabStrip->current = wasPlaying ? 0 : -1;
            tabsChanged();
        });
    menu.addAction("Close all tabs", this, [this] {
        m_tabStrip->tabs.clear();
        m_tabStrip->current = -1;
        tabsChanged();
    });
    menu.exec(global);
}

void MainWindow::startPreview(int index)
{
    // A tab's page without touching what plays (Batin, 2026-11-20): its
    // cover, name, channel, description and what comes with it; mpv goes on
    // with the other video, heard, until Play.
    if (index < 0 || index >= m_tabStrip->tabs.size())
        return;
    VideoTab tab = m_tabStrip->tabs[index];
    if (tab.id.isEmpty())
        tab.id = youtube::videoId(tab.target);
    m_preview = true;
    m_previewTab = tab;
    m_previewItems.clear();
    m_tabStrip->viewing = index;
    tabsChanged();
    m_videoPage->hideUpNext();
    m_coverTarget.clear();
    m_coverId = tab.id;
    m_videoPage->setCover(true, m_covers.get(tab.id, ThumbnailCache::Size::Hero));
    m_videoPage->setPlaying(tab.target, tab.title, tab.channel,
                            tab.id.isEmpty() ? QString() : QStringLiteral("https://www.youtube.com/watch?v=") + tab.id);
    m_videoPage->setChannelName(tab.channel);
    m_videoListPath.clear();   // the playing one's list is read again after
    bool listed = false;
    if (!tab.list.isEmpty() && QFileInfo::exists(tab.list)) {
        const Playlist p = Playlist::peek(tab.list);
        QVector<VideoPage::Entry> entries;
        for (const PlaylistEntry &e : p.readEntries())
            entries.append({e.title, e.target});
        if (entries.size() > 1) {
            m_videoPage->setList(p.name, entries);
            listed = true;
        }
    }
    if (!listed) {
        // On its own: what YouTube suggests after it, as a lone video gets.
        m_videoPage->setList(tab.title, {{tab.title, tab.target}});
        if (!tab.id.isEmpty())
            m_ytClient.related(tab.id, [this, id = tab.id](QVector<ytmusic::Item> items) {
                if (!m_preview || m_previewTab.id != id)
                    return;
                ytmusic::Item self;
                self.kind = ytmusic::Kind::Video;
                self.id = id;
                self.title = m_previewTab.title;
                self.artist = m_previewTab.channel;
                QVector<ytmusic::Item> all{self};
                QSet<QString> seen{id};
                for (const ytmusic::Item &it : items)
                    if (!it.id.isEmpty() && !seen.contains(it.id) && all.size() < 21) {
                        seen.insert(it.id);
                        all << it;
                    }
                m_previewItems = all;
                QVector<VideoPage::Entry> entries;
                for (const ytmusic::Item &it : std::as_const(all))
                    entries.append({it.lineTitle(), it.target()});
                m_videoPage->setList(m_previewTab.title, entries);
                m_videoPage->setPlaying(m_previewTab.target, m_previewTab.title, m_previewTab.channel,
                                        QStringLiteral("https://www.youtube.com/watch?v=") + id);
            });
    }
    m_videoPage->setPlaying(tab.target, tab.title, tab.channel,
                            tab.id.isEmpty() ? QString() : QStringLiteral("https://www.youtube.com/watch?v=") + tab.id);
    if (!tab.id.isEmpty())
        describeVideo(tab.id);
    if (m_pages->currentWidget() != m_videoPage) {
        m_beforeVideo = m_pages->currentWidget();
        m_videoPage->ignorePictureClicks();
        m_pages->setCurrentWidget(m_videoPage);
    }
    if (m_island)
        m_island->refresh();
    schedulePlace();
}

void MainWindow::endPreview(bool showPlaying)
{
    if (!m_preview)
        return;
    m_preview = false;
    m_previewItems.clear();
    m_tabStrip->viewing = -1;
    m_videoPage->setCover(false);
    m_coverTarget.clear();
    tabsChanged();
    if (showPlaying) {
        m_videoListPath.clear();
        updateVideoPage();
    }
    if (m_island)
        m_island->refresh();
    schedulePlace();
}

void MainWindow::playPreview(const QString &target)
{
    // Play on a tab's page: what played stops, this one starts, at the
    // tab's second when it is the tab's own video. The cover stays until
    // mpv has it, so the old picture never shows under its name.
    if (!m_preview)
        return;
    const VideoTab tab = m_previewTab;
    const QString want = target.isEmpty() ? tab.target : target;
    const int index = m_tabStrip->viewing;
    const QVector<ytmusic::Item> items = m_previewItems;
    m_preview = false;
    m_previewItems.clear();
    m_tabStrip->viewing = -1;
    if (index >= 0 && index < m_tabStrip->tabs.size())
        m_tabStrip->current = index;
    tabsChanged();
    m_tabPending = want;
    QTimer::singleShot(20000, this, [this, want] { if (m_tabPending == want) m_tabPending.clear(); });
    m_coverTarget = want;
    m_coverArrived = false;
    m_startSeconds = want == tab.target && tab.seconds > 5 ? tab.seconds - 2 : 0;
    if (!tab.list.isEmpty() && QFileInfo::exists(tab.list)) {
        const QVector<PlaylistEntry> entries = Playlist::peek(tab.list).readEntries();
        for (int i = 0; i < entries.size(); ++i)
            if (entries[i].target == want) {
                play(tab.list, false, i);
                return;
            }
    }
    if (!items.isEmpty()) {
        for (int i = 0; i < items.size(); ++i)
            if (items[i].target() == want) {
                playFound(items, tab.title, i, false, false);
                return;
            }
    }
    ytmusic::Item item;
    item.kind = ytmusic::Kind::Video;
    item.id = youtube::videoId(want).isEmpty() ? tab.id : youtube::videoId(want);
    item.title = tab.title;
    item.artist = tab.channel;
    if (item.id.isEmpty()) {
        m_startSeconds = 0;
        m_videoPage->setCover(false);
        return;
    }
    playFound({item}, tab.title, 0, false, false);
}

void MainWindow::showAllTabs(const QPoint &belowRight)
{
    // Every tab as a list under the chip, the cover, the name and the
    // channel; a click plays it, its X closes it.
    auto *popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setAttribute(Qt::WA_TranslucentBackground);
    auto *body = new QFrame(popup);
    body->setObjectName("tabsPopup");
    const theme::Palette &c = theme::c();
    body->setStyleSheet(QStringLiteral("QFrame#tabsPopup { background: %1; border: 1px solid %2; border-radius: 12px; }"
                                       "QLabel { color: %3; background: transparent; }"
                                       "QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: none; }")
                            .arg(c.popup.name(), c.line.name(), c.muted.name()));
    auto *outer = new QVBoxLayout(popup);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(body);
    auto *box = new QVBoxLayout(body);
    box->setContentsMargins(8, 10, 8, 8);
    box->setSpacing(4);
    auto *head = new QLabel(QStringLiteral("Tabs  ·  %1").arg(m_tabStrip->tabs.size()));
    head->setContentsMargins(8, 0, 0, 2);
    box->addWidget(head);
    auto *list = new TabStrip(true);
    list->cover = m_tabStrip->cover;
    const auto refill = [this, list, head] {
        list->tabs = m_tabStrip->tabs;
        list->current = m_tabStrip->current;
        list->changed();
        head->setText(QStringLiteral("Tabs  ·  %1").arg(m_tabStrip->tabs.size()));
    };
    refill();
    auto *scroll = new QScrollArea;
    scroll->setWidget(list);
    scroll->setWidgetResizable(false);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedWidth(list->width() + 12);
    const auto fit = [scroll, list, popup] {
        scroll->setFixedHeight(qMin(list->height(), 9 * qRound(58 * theme::chromeZoom()) + 8));
        popup->adjustSize();
    };
    fit();
    box->addWidget(scroll);
    connect(&m_covers, &ThumbnailCache::ready, list, [list] { list->update(); });
    list->onPick = [this, popup](int i) {
        popup->close();
        pickTab(i);
    };
    list->onClose = [this, popup, refill, fit](int i) {
        const bool playing = i == m_tabStrip->current;
        closeTab(i);
        if (m_tabStrip->tabs.size() < 2 || playing) {
            popup->close();
            return;
        }
        refill();
        fit();
    };
    list->onMenu = [this, popup](int i, const QPoint &at) {
        popup->close();
        tabMenu(i, at);
    };
    popup->adjustSize();
    QPoint at(belowRight.x() - popup->width(), belowRight.y());
    if (const QScreen *screen = QGuiApplication::screenAt(belowRight)) {
        const QRect area = screen->availableGeometry();
        at.setX(qBound(area.left(), at.x(), area.right() - popup->width()));
        at.setY(qMin(at.y(), area.bottom() - popup->height()));
    }
    popup->move(at);
    popup->show();
}

void MainWindow::fitTabs()
{
    // From the search to the first button on the right, less the island's
    // room: past that the tabs go behind the "+N" chip (Batin, 2026-11-20:
    // a full strip squeezed the island, then ran under it).
    if (!m_tabStrip || !m_topBar || !m_tabStrip->isVisible())
        return;
    QWidget *leftmost = m_search->isVisible() ? static_cast<QWidget *>(m_search) : m_searchButton;
    QWidget *rightmost = m_updateButton->isVisible() ? m_updateButton
                       : m_profileButton->isVisible() ? m_profileButton : m_gearButton;
    const int left = leftmost->mapTo(m_topBar, QPoint(leftmost->width(), 0)).x() + 8;
    const int right = rightmost->mapTo(m_topBar, QPoint(0, 0)).x();
    // The island is centred between the two; the tabs end before its half.
    const int centre = (left - 8 + right) / 2;
    const int half = qRound(235 * theme::chromeZoom());
    const int space = centre - half - left - 12;
    // Folded behind "Tabs N" by the setting, or when not even one tab and
    // the "+N" chip fit beside the island.
    m_tabStrip->collapsed = false;
    const int unfolded = m_tabStrip->smallest();
    m_tabStrip->collapsed = QSettings().value("youtube/collapsedTabs", false).toBool() || space < unfolded;
    const int room = qMax(m_tabStrip->smallest(), space);
    m_tabStrip->changed();
    if (m_tabStrip->maximumWidth() != room) {
        m_tabStrip->setMaximumWidth(room);
        if (m_island)
            QTimer::singleShot(0, m_island, [this] { m_island->refresh(); });
    }
}

void MainWindow::togglePause()
{
    // A video stopped at its end (keep-open): Play starts it again, as
    // YouTube's does (Batin, 2026-11-20: the button did nothing there).
    if (m_mpv.property("eof-reached").toBool()) {
        m_videoPage->hideUpNext();
        m_mpv.command({"seek", 0, "absolute"});
        m_mpv.setProperty("pause", false);
        return;
    }
    m_mpv.command({"cycle", "pause"});
}

void MainWindow::fillAddTo(QMenu *to, const QVector<PlaylistEntry> &entries, const QString &skip, bool videos)
{
    const auto addTo = [this, entries](const QString &path) {
        const Playlist target = Playlist::peek(path);
        QSet<QString> has;
        for (const PlaylistEntry &e : target.readEntries())
            has << e.target;
        QVector<PlaylistEntry> adding;
        for (const PlaylistEntry &e : entries)
            if (!has.contains(e.target)) {
                adding << e;
                has << e.target;
            }
        if (adding.isEmpty()) {
            toast(QStringLiteral("Already in %1").arg(target.name));
            return;
        }
        if (!Playlist::append(path, adding)) {
            QMessageBox::warning(this, "Add to playlist", "The playlist file couldn't be written.");
            return;
        }
        toast(adding.size() == 1 ? QStringLiteral("Added to %1").arg(target.name)
                                 : QStringLiteral("%1 songs added to %2").arg(adding.size()).arg(target.name));
        rescan();
    };
    to->addAction("New playlist...", this, [this, addTo, videos] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "New playlist", "Playlist name:", QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        const QString path = m_library.createPlaylist(name, QString(), false, !videos);
        if (path.isEmpty()) {
            QMessageBox::warning(this, "New playlist", "The playlist couldn't be made.");
            return;
        }
        addTo(path);
    });
    to->addSeparator();
    const QString home = Library::homeFolder();
    for (const Playlist &p : m_library.playlists()) {
        const QString dir = QDir::cleanPath(QFileInfo(p.path).absolutePath());
        const bool ours = dir.compare(home, Qt::CaseInsensitive) == 0 || dir.startsWith(home + '/', Qt::CaseInsensitive);
        if (!ours || p.path == skip)
            continue;   // lists outside the library are the bot's to write
        // Each side its own lists: a Spotify user's many song lists buried
        // the few video lists on the YouTube side (Batin, 2026-11-20).
        if (p.audio == videos)
            continue;
        const QString path = p.path;
        to->addAction(p.folder.isEmpty() ? p.name : p.folder + QStringLiteral("  ›  ") + p.name, this, [addTo, path] { addTo(path); });
    }
}

bool MainWindow::likedHas(const QString &target)
{
    QString id = youtube::videoId(target);
    if (id.isEmpty())
        id = idForTarget(target);
    const QString path = likedPath(false);
    if (path.isEmpty() || id.isEmpty())
        return false;
    for (const PlaylistEntry &e : Playlist::peek(path).readEntries())
        if (youtube::videoId(e.target) == id)
            return true;
    return false;
}

// The + of Liked songs, right-clicked: the lists to add the song to at once,
// as Spotify's, and out of Liked songs when it is there (2026-11-11).
void MainWindow::likeMenu(const QString &target, const QString &title, const QPoint &global)
{
    if (target.isEmpty())
        return;
    QMenu menu(this);
    if (likedHas(target)) {
        menu.addAction("Remove from Liked songs", this, [this, target, title] { toggleLiked(target, title); });
        menu.addSeparator();
    }
    QMenu *to = &menu;
    to->addSection(QStringLiteral("Add to playlist"));
    fillAddTo(to, {PlaylistEntry{title, target, {}}}, likedPath(false));
    menu.exec(global);
}

// A song's link on YouTube Music, a video's on YouTube, on the clipboard.
void MainWindow::copyLink(const QString &id, bool song)
{
    QGuiApplication::clipboard()->setText(song ? QStringLiteral("https://music.youtube.com/watch?v=") + id
                                               : QStringLiteral("https://www.youtube.com/watch?v=") + id);
    toast(song ? QStringLiteral("Song link copied") : QStringLiteral("Video link copied"));
}

namespace {
// An mpv option value that may hold commas: "%<bytes>%<text>".
QString mpvQuoted(const QString &text)
{
    return QLatin1Char('%') + QString::number(text.toUtf8().size()) + QLatin1Char('%') + text;
}
} // namespace

void MainWindow::queueSongs(const QVector<PlaylistEntry> &songs, bool next)
{
    if (songs.isEmpty())
        return;
    if (!m_mpv.connected() || m_mpv.property("playlist-pos").toInt() < 0) {
        toast("Play something first, then add to the queue.");
        return;
    }
    const QVariantList entries = m_mpv.property("playlist").toList();
    const int pos = m_mpv.property("playlist-pos").toInt();
    int at = pos + 1;
    // Add to queue goes after what is queued already; Play next before it.
    if (!next)
        while (at < entries.size() && m_queueIds.contains(entries[at].toMap().value("id").toLongLong()))
            ++at;
    for (const PlaylistEntry &e : songs) {
        QVariantList command{"loadfile", e.target, "insert-at", at++};
        if (!e.title.isEmpty())
            command << QStringLiteral("force-media-title=") + mpvQuoted(e.title);
        m_mpv.command(command);
        m_queuePending << e.target;
        m_queueTitles.insert(e.target, e.title);
    }
    if (next)
        toast(songs.size() == 1 ? QStringLiteral("Plays next") : QStringLiteral("%1 play next").arg(songs.size()));
    else
        toast(songs.size() == 1 ? QStringLiteral("Added to queue") : QStringLiteral("%1 added to queue").arg(songs.size()));
}

void MainWindow::syncQueue()
{
    const QVariantList entries = m_mpv.property("playlist").toList();
    const int pos = m_mpv.property("playlist-pos").toInt();
    QSet<qint64> ids;
    for (const QVariant &v : entries) {
        const QVariantMap entry = v.toMap();
        const qint64 id = entry.value("id").toLongLong();
        ids.insert(id);
        // A new entry with a target the queue just loaded is the queue's.
        if (!m_knownIds.contains(id) && m_queuePending.removeOne(entry.value("filename").toString()))
            m_queueIds.insert(id);
    }
    // A new list replaced the old one (no entry left from it): what
    // Autoplay put in went with it, and must not label the new list's songs.
    if (!m_knownIds.isEmpty() && !m_knownIds.intersects(ids)) {
        m_autoplayTitles.clear();
        m_autoplayFor.clear();
    }
    m_knownIds = ids;
    m_queueIds.intersect(ids);   // a new list replaced them
    // What is queued changed what the row marks and the "N of M" say.
    QTimer::singleShot(0, this, [this] { markPlaying(); });
    if (m_queueIds.isEmpty()) {
        if (m_queuePending.isEmpty())
            m_queueTitles.clear();
        return;
    }
    // Played: out of the list, from the end so the places hold.
    for (int i = qMin(pos, int(entries.size())) - 1; i >= 0; --i) {
        const qint64 id = entries[i].toMap().value("id").toLongLong();
        if (m_queueIds.contains(id)) {
            m_queueIds.remove(id);
            m_mpv.command({"playlist-remove", i});
        }
    }
}

bool MainWindow::playingAdded() const
{
    return playingQueued() || m_autoplayTitles.contains(m_mpv.property("path").toString());
}

QString MainWindow::positionText()
{
    // The list's own order: queued songs and Autoplay's are not counted in
    // it, and say where they come from instead, as Spotify does (a queue of
    // two in a list of three read "1 of 5", 2026-11-11).
    if (!m_mpv.connected())
        return {};
    if (playingQueued())
        return QStringLiteral("From queue");
    if (m_autoplayTitles.contains(m_mpv.property("path").toString()))
        return QStringLiteral("Autoplay");
    const QVariantList entries = m_mpv.property("playlist").toList();
    int added = 0;
    for (const QVariant &v : entries) {
        const QVariantMap e = v.toMap();
        if (m_queueIds.contains(e.value("id").toLongLong()) || m_autoplayTitles.contains(e.value("filename").toString()))
            ++added;
    }
    const int count = int(entries.size()) - added;
    if (count <= 0)
        return {};
    int number = m_nowPlaying->numberFor ? m_nowPlaying->numberFor() : 0;
    if (number <= 0) {
        const int pos = m_mpv.property("playlist-pos").toInt();
        number = pos + 1;
        for (int i = 0; i < qMin(pos, int(entries.size())); ++i)
            if (m_queueIds.contains(entries[i].toMap().value("id").toLongLong()))
                --number;
    }
    return QStringLiteral("%1 of %2").arg(number).arg(count);
}

bool MainWindow::playingQueued() const
{
    const QVariantList entries = m_mpv.property("playlist").toList();
    const int pos = m_mpv.property("playlist-pos").toInt();
    return pos >= 0 && pos < entries.size() && m_queueIds.contains(entries[pos].toMap().value("id").toLongLong());
}

void MainWindow::maybeAutoplay()
{
    if (!m_mpv.connected() || !QSettings().value("playback/autoplay", true).toBool() ||
        !m_mpv.property("user-data/ghost/audio").toBool())
        return;
    const auto looping = [](const QVariant &v) {
        return v.isValid() && v.toString() != QLatin1String("no") && v.toString() != QLatin1String("false");
    };
    if (looping(m_mpv.property("loop-playlist")) || looping(m_mpv.property("loop-file")))
        return;   // repeat on: the list itself comes again
    const int pos = m_mpv.property("playlist-pos").toInt();
    const int count = m_mpv.property("playlist-count").toInt();
    if (count <= 0 || pos != count - 1)
        return;
    const QVariantList entries = m_mpv.property("playlist").toList();
    const QString target = entries.value(pos).toMap().value("filename").toString();
    QString id = youtube::videoId(target);
    if (id.isEmpty())
        id = idForTarget(target);
    if (id.isEmpty() || id == m_autoplayFor)
        return;
    m_autoplayFor = id;
    m_ytClient.radio(id, [this, id](QVector<ytmusic::Item> items) {
        // Only while that song is still the last one playing.
        const QVariantList now = m_mpv.property("playlist").toList();
        const int at = m_mpv.property("playlist-pos").toInt();
        if (m_autoplayFor != id || at != now.size() - 1 || items.isEmpty())
            return;
        QSet<QString> have;
        for (const QVariant &v : now)
            have.insert(youtube::videoId(v.toMap().value("filename").toString()));
        int added = 0;
        for (const ytmusic::Item &it : items) {
            if (it.id.isEmpty() || it.id == id || have.contains(it.id) || added >= 25)
                continue;
            have.insert(it.id);
            const QString title = it.lineTitle();
            m_mpv.command({"loadfile", it.target(), "append", -1, QStringLiteral("force-media-title=") + mpvQuoted(title)});
            m_autoplayTitles.insert(it.target(), title);
            ++added;
        }
        if (added > 0)
            toast(QStringLiteral("Autoplay: songs like this one come next"));
    });
}

void MainWindow::applyNormalize()
{
    if (!m_mpv.connected())
        return;
    // ffmpeg's dynaudnorm, set slow: 500 ms frames and a 31-frame window
    // move the gain over about 15 seconds, so a song's own build-ups and
    // quiet parts stay and nothing pumps; it aims peaks at 0.9 (headroom,
    // no clipping) and lifts at most 4x (+12 dB), so a hushed intro or a
    // noisy live take is not blown up. No compression (s=0). Measured on
    // mpv 0.41: it adds no start-up delay. On by default since Batın heard
    // it (2026-11-11): the difference is clear and it stops the loud jumps.
    // Turned off, it stays off (the setting is kept).
    m_mpv.command({"af", "remove", "@ghostnorm"});
    if (QSettings().value("playback/normalize", true).toBool())
        m_mpv.command({"af", "add", "@ghostnorm:lavfi=[dynaudnorm=f=500:g=31:p=0.9:m=4]"});
}

void MainWindow::playLog(const QString &line)
{
    QFile log(QDir::temp().filePath(QStringLiteral("ghost-play.log")));
    if (log.size() > 256 * 1024)
        log.remove();
    if (log.open(QIODevice::Append | QIODevice::Text))
        log.write((QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz ")) + line + '\n').toUtf8());
}

bool MainWindow::processAlive(qint64 pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h)
        return false;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

// -- back and forward ---------------------------------------------------------

MainWindow::Place MainWindow::herePlace()
{
    Place p;
    p.youtube = m_youtubeLayout;
    QWidget *now = m_pages->currentWidget();
    if (now == m_videoPage) {
        // Each video its own place: Back from one opened on the page goes
        // to the one before, at its second (Batin, 2026-11-20).
        const QString path = m_mpv.connected() ? m_mpv.property("path").toString() : QString();
        const QString list = playingListPath();
        QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString());
        if (id.isEmpty())
            id = youtube::videoId(path);
        const QString title = m_mpv.property("media-title").toString();
        p.key = QStringLiteral("video:") + path;
        p.go = [this, path, list, id, title] {
            if (!m_mpv.connected() || path.isEmpty())
                return;
            if (m_mpv.property("path").toString() == path) {
                if (!m_mpv.property("user-data/ghost/audio").toBool())
                    showVideoPage();
                return;
            }
            const double at = m_videoSeconds.value(path);
            m_startSeconds = at > 5 ? at - 2 : 0;
            m_videoArrival = path;
            QTimer::singleShot(20000, this, [this, path] { if (m_videoArrival == path) m_videoArrival.clear(); });
            if (!list.isEmpty() && QFileInfo::exists(list)) {
                const QVector<PlaylistEntry> entries = Playlist::peek(list).readEntries();
                for (int i = 0; i < entries.size(); ++i)
                    if (entries[i].target == path) {
                        play(list, false, i);
                        return;
                    }
            }
            if (id.isEmpty()) {
                m_startSeconds = 0;
                m_videoArrival.clear();
                return;
            }
            ytmusic::Item item;
            item.kind = ytmusic::Kind::Video;
            item.id = id;
            item.title = title;
            playFound({item}, title, 0, false, false);
        };
    } else if (now == m_lyricsView) {
        p.key = QStringLiteral("lyrics");
        p.go = [this] {
            if (m_pages->currentWidget() != m_lyricsView)
                toggleLyrics();
        };
    } else if (now == m_settings) {
        p.key = QStringLiteral("settings");
        p.go = [this] { showSettings(); };
    } else if (now == m_shortcutsScroll) {
        p.key = QStringLiteral("shortcuts");
        p.go = [this] { showShortcuts(); };
    } else if (now == m_profileScroll) {
        p.key = QStringLiteral("profile");
        p.go = [this] { showProfile(); };
    } else if (now == m_searchPage) {
        const QString text = m_search->text();
        const std::function<void()> view = m_searchPage->viewRestore();
        // Which of the YouTube menu's rows was lit (Home, History...): lit again.
        const int nav = m_ytMark;
        p.key = QStringLiteral("search:") + m_searchPage->viewKey();
        p.go = [this, text, view, nav] {
            QWidget *was = m_pages->currentWidget();
            if (was != m_searchPage)
                m_beforeSearch = was == m_videoPage || was == m_lyricsView ? m_pages->widget(0) : was;
            {
                const QSignalBlocker quiet(m_search);
                m_search->setText(text);
            }
            m_searchTimer.stop();
            if (view)
                view();
            m_pages->setCurrentWidget(m_searchPage);
            markYouTubeMenu(nav);
        };
    } else if (m_pages->currentIndex() == 1 && m_open >= 0 && m_open < m_library.playlists().size()) {
        // By its file: the indexes move as lists are added or sorted.
        const QString path = m_library.playlists()[m_open].path;
        p.key = QStringLiteral("list:") + path;
        p.go = [this, path] {
            const auto &all = m_library.playlists();
            for (int i = 0; i < all.size(); ++i)
                if (all[i].path == path) {
                    openPlaylist(i);
                    return;
                }
            showHome();   // the list is gone
        };
    } else if (m_pages->currentIndex() == 0) {
        p.key = QStringLiteral("home");
        p.go = [this] { showHome(); };
    } else {
        const int index = m_pages->currentIndex();
        p.key = QStringLiteral("page:%1").arg(index);
        p.go = [this, index] { m_pages->setCurrentIndex(index); };
    }
    return p;
}

void MainWindow::noteVisit()
{
    // Back went to a video mpv has not opened yet: noted once it has.
    if (!m_videoArrival.isEmpty()) {
        if (m_mpv.property("path").toString() != m_videoArrival)
            return;
        m_videoArrival.clear();
    }
    const Place now = herePlace();
    if (m_navigating) {
        // Arrived by Back or Forward: no new step.
        m_navigating = false;
        m_here = now;
        updateHistoryButtons();
        return;
    }
    if (now.key == m_here.key && now.youtube == m_here.youtube) {
        m_here = now;   // the same place, maybe filled in since ("Following: loading...")
        return;
    }
    if (!m_here.key.isEmpty()) {
        m_backPlaces << m_here;
        if (m_backPlaces.size() > 50)
            m_backPlaces.removeFirst();
    }
    m_forwardPlaces.clear();
    m_here = now;
    updateHistoryButtons();
}

void MainWindow::goHistory(bool forward)
{
    QVector<Place> &from = forward ? m_forwardPlaces : m_backPlaces;
    QVector<Place> &to = forward ? m_backPlaces : m_forwardPlaces;
    if (from.isEmpty())
        return;
    m_visitSoon.stop();
    const Place target = from.takeLast();
    to << herePlace();
    m_navigating = true;
    if (m_videoFull)
        setVideoFullScreen(false);
    if (target.youtube != m_youtubeLayout)
        setYouTubeLayout(target.youtube, true, target.go);
    else if (target.go)
        target.go();
    // Even when nothing changed on screen, the arrival is noted.
    m_visitSoon.start();
}

void MainWindow::updateHistoryButtons()
{
    if (m_backButton)
        m_backButton->setEnabled(!m_backPlaces.isEmpty());
    if (m_forwardButton)
        m_forwardButton->setEnabled(!m_forwardPlaces.isEmpty());
}

// -- Recommended songs -----------------------------------------------------------

QWidget *MainWindow::buildRecommended()
{
    // A sheet rising from the page's bottom (see RecSheet): shut, only its
    // header row shows; opened, it grows upward over the page's room
    // (Batin, 2026-11-12: first a dark rule, then a card apart whose gap
    // read as a black strip).
    auto *sheet = new RecSheet;
    m_recCard = sheet;
    m_recCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *cardBox = new QVBoxLayout(m_recCard);
    // As much under the header row as the grip's band over it, so the row
    // sits in the middle of the shut sheet (it sat low, 2026-11-12).
    cardBox->setContentsMargins(20, RecSheet::grip(), 16, RecSheet::grip());
    m_recBox = new QWidget;
    cardBox->addWidget(m_recBox);
    auto *column = new QVBoxLayout(m_recBox);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);
    auto *head = new QHBoxLayout;
    m_recToggle = new QPushButton;
    m_recToggle->setObjectName("columnButton");
    m_recToggle->setCursor(Qt::PointingHandCursor);
    m_recToggle->setToolTip("Show or hide songs recommended for this list");
    theme::style(m_recToggle, "font-size: 12pt; font-weight: 700; color: @text;");
    m_recRefresh = new QPushButton("Refresh");
    m_recRefresh->setObjectName("link");
    m_recRefresh->setCursor(Qt::PointingHandCursor);
    m_recRefresh->setToolTip("Other songs");
    m_recStatus = new QLabel;
    m_recStatus->setObjectName("heroInfo");
    head->addWidget(m_recToggle);
    head->addSpacing(10);
    head->addWidget(m_recStatus);
    head->addStretch();
    head->addWidget(m_recRefresh);
    head->addSpacing(20);
    column->addLayout(head);
    m_recList = new QListWidget;
    m_recList->setObjectName("songs");
    m_recList->setMouseTracking(true);
    m_recList->setUniformItemSizes(true);
    m_recList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_recList->setItemDelegate(new tiles::SongDelegate([this](const QModelIndex &index) {
        return m_covers.get(index.data(Qt::UserRole).toString(), ThumbnailCache::Size::Card);
    }, m_recList));
    m_recList->viewport()->installEventFilter(this);
    m_recList->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    column->addWidget(m_recList, 1);
    m_recWanted = QSettings().value("recommended/height", 0).toInt();

    // Pulled by its top edge (the grip, the header row) it follows the
    // pointer; let go near the bottom it shuts, elsewhere it stays at that
    // height, kept for next time (Batin, 2026-11-12).
    sheet->shutHeight = [this] { return recShutHeight(); };
    sheet->onClick = [this] {
        m_recAutoArmed = false;
        setRecommendedOpen(!m_recOpen);
    };
    sheet->onDrag = [this](int height) {
        // Pulled past shut (below the song bar) the height goes under 0,
        // and -1 is "no drag": the sheet was drawn open until let go
        // (Batin, 2026-11-20).
        m_recDragHeight = qMax(0, height);
        fitRecSheet();
    };
    sheet->onDragEnd = [this](int height) {
        m_recDragHeight = -1;
        m_recAutoArmed = false;
        const int shut = recShutHeight();
        if (height < shut + 40) {
            setRecommendedOpen(false);
            return;
        }
        const int natural = recNaturalHeight();
        m_recWanted = height >= natural - 8 ? 0 : qMax(height, shut);
        QSettings().setValue("recommended/height", m_recWanted);
        setRecommendedOpen(true);
    };

    // Shut by default (it takes no room until asked for), opened with its
    // header or by scrolling the list to its end (2026-11-12).
    setRecommendedOpen(false);
    connect(m_recToggle, &QPushButton::clicked, this, [this] {
        m_recAutoArmed = false;
        setRecommendedOpen(!m_recOpen);
    });
    connect(m_songs->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        QScrollBar *bar = m_songs->verticalScrollBar();
        // Only on reaching the end by scrolling: shutting it makes the list
        // longer and its bar lands on the end again, which reopened it at
        // once (Batin, 2026-11-12: pulled down past the window's edge, it
        // sprang back open). Armed again once the list is scrolled up.
        if (value < bar->maximum() - 4)
            m_recAutoArmed = true;
        if (value < bar->maximum() || bar->maximum() <= 0 || m_recOpen || !m_recBox->isVisible()
            || m_recShown.isEmpty() || !m_recAutoArmed || m_recDragHeight >= 0)
            return;
        m_recAutoArmed = false;
        setRecommendedOpen(true);
        // The list got shorter: keep its last songs in sight above the rule.
        QTimer::singleShot(0, this, [this] { m_songs->scrollToBottom(); });
    });
    connect(m_recRefresh, &QPushButton::clicked, this, [this] {
        if (m_recPool.size() < 5)
            loadRecommended(false, true);
        else
            showRecommended(true);
    });
    connect(m_recList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const ytmusic::Item song = m_recShown.value(m_recList->row(item));
        if (!song.id.isEmpty())
            m_searchPage->playRadio(song);
    });
    m_recList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_recList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_recList->itemAt(pos);
        if (!item)
            return;
        const int row = m_recList->row(item);
        QMenu menu(this);
        menu.addAction("Add to this playlist", this, [this, row] { addRecommended(row); });
        menu.addAction("Play with its radio", this, [this, row] { m_searchPage->playRadio(m_recShown.value(row)); });
        menu.addAction("Go to artist", this, [this, row] { goToArtist(m_recShown.value(row).artist); });
        menu.exec(m_recList->viewport()->mapToGlobal(pos));
    });
    m_recBox->hide();
    m_recCard->hide();
    return m_recCard;
}

void MainWindow::updateRecCard()
{
    // Only under a list's page, and only once there is something for it.
    if (m_recCard)
        m_recCard->setVisible(m_recBox && !m_recBox->isHidden() && m_pages && m_pages->currentWidget() == m_listPage);
}

void MainWindow::setRecommendedOpen(bool open)
{
    m_recOpen = open;
    m_recToggle->setText(QStringLiteral("Recommended  %1").arg(open ? QStringLiteral("\u25BE") : QStringLiteral("\u25B8")));
    m_recRefresh->setVisible(open);
    fitRecSheet();
}

int MainWindow::recShutHeight() const
{
    // The grip's band, the header row, as much again under it.
    int row = 0;
    for (QWidget *w : {static_cast<QWidget *>(m_recToggle), static_cast<QWidget *>(m_recStatus), static_cast<QWidget *>(m_recRefresh)})
        row = qMax(row, w->sizeHint().height());
    return 2 * RecSheet::grip() + row;
}

int MainWindow::recNaturalHeight() const
{
    // Open with every song shown: the shut sheet, the gap, the rows.
    const int rows = m_recList->count();
    if (rows == 0)
        return recShutHeight();
    return recShutHeight() + 4 + int(56 * theme::zoom()) * rows + 4;
}

void MainWindow::fitRecSheet()
{
    // The sheet's height: shut, open (all of its songs, or the height it was
    // left at), or where a drag has it. At most ~70% of the page card.
    const int shut = recShutHeight();
    // At most ~70% of the page card, and never so much that the list's page
    // has less than its folded header, its buttons, column names and two
    // songs: a smaller window, or one made smaller later, pulls a height
    // kept from before down to that too.
    const int panel = m_mainPanel ? m_mainPanel->height() : 900;
    const int limit = qMax(shut, qMin(int(panel * 0.7), panel - listheader::folded() - listPageOverhead()));
    const int most = qMax(shut, qMin(recNaturalHeight(), limit));
    int height = shut;
    if (m_recDragHeight >= 0)
        height = qBound(shut, m_recDragHeight, limit);
    else if (m_recOpen)
        height = qBound(shut, m_recWanted > 0 ? m_recWanted : recNaturalHeight(), most);
    m_recList->setVisible(height > shut + 4);
    m_recCard->setFixedHeight(height);
}

void MainWindow::loadRecommended(bool fresh, bool advance)
{
    if (m_open < 0 || m_library.playlists()[m_open].path != m_recFor)
        return;
    if (fresh) {
        m_recPool.clear();
        m_recShown.clear();
        m_recSeen.clear();
        m_recSeed = 0;
        m_recList->clear();
    }
    const QVector<PlaylistEntry> entries = m_library.playlists()[m_open].readEntries();
    // The radio of a song near the end: the last first, then one further
    // back each time the shown ones run out. Lists made before the app kept
    // video ids (searches, imports, files) have none in their lines: the id
    // the covers were found with is used, else the song is looked up by its
    // name (2026-11-12: such lists only said "Nothing more to find").
    QString seed, query;
    for (int back = m_recSeed; back < entries.size() && seed.isEmpty() && query.isEmpty(); ++back) {
        const PlaylistEntry &e = entries[entries.size() - 1 - back];
        seed = idForTarget(e.target);
        if (seed.isEmpty()) {
            query = youtube::searchQuery(e.target);
            if (query.isEmpty())
                query = e.title.trimmed();
        }
        m_recSeed = back + 3;
    }
    m_recBox->show();
    updateRecCard();
    if (seed.isEmpty() && query.isEmpty()) {
        m_recStatus->setText(entries.isEmpty() ? QStringLiteral("Add a song first: they are found from its radio.")
                                               : QStringLiteral("Nothing more to find."));
        m_recList->clear();
        fitRecSheet();
        return;
    }
    m_recStatus->setText("Finding songs...");
    const int serial = ++m_recSerial;
    const QString forList = m_recFor;
    const auto askRadio = [this, serial, forList, advance](const QString &id) {
        m_ytClient.radio(id, [this, serial, forList, advance](QVector<ytmusic::Item> items) {
            if (serial != m_recSerial || forList != m_recFor)
                return;
            for (const ytmusic::Item &it : items)
                if (!it.id.isEmpty() && !m_recSeen.contains(it.id)) {
                    m_recSeen.insert(it.id);
                    m_recPool << it;
                }
            showRecommended(advance);
        });
    };
    if (!seed.isEmpty()) {
        askRadio(seed);
        return;
    }
    // Videos, not songs: a song search would end one the user is making.
    m_ytClient.search(query, ytmusic::Kind::Video, [this, serial, forList, advance, askRadio](QVector<ytmusic::Item> items) {
        if (serial != m_recSerial || forList != m_recFor)
            return;
        if (items.isEmpty() || items.first().id.isEmpty())
            showRecommended(advance);   // what there is; Refresh tries further back
        else
            askRadio(items.first().id);
    });
}

void MainWindow::showRecommended(bool refresh)
{
    if (m_open < 0 || m_library.playlists()[m_open].path != m_recFor)
        return;
    // Never one already in the list (added since, or there from the start).
    QSet<QString> inList;
    for (const PlaylistEntry &e : m_library.playlists()[m_open].readEntries())
        inList.insert(idForTarget(e.target));
    const auto keep = [&inList](const ytmusic::Item &it) { return !inList.contains(it.id); };
    QVector<ytmusic::Item> next;
    QVector<ytmusic::Item> stillShown;
    for (const ytmusic::Item &it : m_recShown)
        if (keep(it))
            stillShown << it;
    // Refresh (the shown ones all still there, and more in the pool): the next five.
    if (!refresh && stillShown.size() >= 5) {
        next = stillShown;
    } else {
        if (!refresh)
            next = stillShown;
        while (next.size() < 5 && !m_recPool.isEmpty()) {
            const ytmusic::Item it = m_recPool.takeFirst();
            if (keep(it))
                next << it;
        }
    }
    m_recShown = next;
    m_recList->clear();
    for (int i = 0; i < m_recShown.size(); ++i) {
        const ytmusic::Item &it = m_recShown[i];
        auto *row = new QListWidgetItem(it.title);
        row->setData(Qt::UserRole, it.id);
        row->setData(tiles::kArtistRole, it.artist);
        row->setData(tiles::kNumberRole, QString::number(i + 1));
        row->setData(tiles::kDurationRole, it.duration.isEmpty() ? QStringLiteral(" ") : it.duration);
        row->setToolTip("Double-click to play, + to add to this playlist");
        m_recList->addItem(row);
    }
    fitRecSheet();
    m_recStatus->setText(m_recShown.isEmpty() ? QStringLiteral("Nothing found this time.")
                                              : QStringLiteral("Based on the songs at the end of this list"));
    m_recBox->show();
    updateRecCard();
    // Too few left for the next Refresh: ask further back now.
    if (m_recPool.size() < 5 && !m_recShown.isEmpty() && refresh)
        loadRecommended(false);
}

void MainWindow::addRecommended(int row)
{
    if (m_open < 0 || row < 0 || row >= m_recShown.size())
        return;
    const ytmusic::Item song = m_recShown[row];
    const QString path = m_library.playlists()[m_open].path;
    if (!Playlist::append(path, {PlaylistEntry{song.lineTitle(), song.target(), {}}})) {
        toast("The playlist couldn't be written.");
        return;
    }
    toast(QStringLiteral("Added to %1").arg(m_library.playlists()[m_open].name));
    // The list shows it at its end; this one's place takes the next.
    rescan();
}

// -- A list's description ----------------------------------------------------------

void MainWindow::showDescription()
{
    if (!m_playlistDesc || m_open < 0)
        return;
    const QString text = m_library.playlists()[m_open].description;
    // On the open header only, and not in the slim bar it folds into.
    const bool room = m_playlistCover->height() > 200 * theme::zoom();
    const QString shown = text.isEmpty() ? QStringLiteral("Add a description") : QString(text).replace('\n', QStringLiteral("  "));
    m_playlistDesc->setText(QFontMetrics(m_playlistDesc->font()).elidedText(shown, Qt::ElideRight, qMax(60, m_playlistCover->width() - static_cast<PlaylistHeader *>(m_playlistCover)->coverRect().right() - 60)));
    m_playlistDesc->setToolTip(text.isEmpty() ? QStringLiteral("Click to add a description")
                                              : text + QStringLiteral("\n\nClick to edit"));
    theme::style(m_playlistDesc, text.isEmpty() ? "color: @muted; font-style: italic;" : "color: @textSoft;");
    m_playlistDesc->setVisible(room);
}

void MainWindow::editDescription(int index)
{
    const auto &all = m_library.playlists();
    if (index < 0 || index >= all.size())
        return;
    const QString path = all[index].path;
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(this, "Description", QStringLiteral("A few words about %1:").arg(all[index].name),
                                                        all[index].description, &ok);
    if (!ok)
        return;
    if (!Playlist::writeDescription(path, text.left(300))) {
        toast("The playlist file couldn't be written.");
        return;
    }
    rescan();
}

void MainWindow::setPrivateSession(bool on)
{
    if (m_private == on)
        return;
    m_private = on;
    m_settings->setPrivate(on);
    m_nowPlaying->setPrivate(on);
    m_profileButton->setToolTip(on ? QStringLiteral("Profile (private session)") : QStringLiteral("Profile"));
    if (m_applyPresence)
        m_applyPresence();
    toast(on ? QStringLiteral("Private session on: nothing is written down, Discord shows nothing")
             : QStringLiteral("Private session off"));
}

void MainWindow::sleepMenu(const QPoint &global)
{
    QMenu menu(this);
    const bool on = m_sleep.isActive() || m_sleepAtSongEnd;
    QString state = QStringLiteral("Sleep timer");
    if (m_sleep.isActive())
        state += QStringLiteral(": %1 min left").arg(qMax(1, (m_sleep.remainingTime() + 59999) / 60000));
    else if (m_sleepAtSongEnd)
        state += QStringLiteral(": end of this song");
    menu.addAction(state)->setEnabled(false);
    menu.addSeparator();
    for (int minutes : {5, 15, 30, 45, 60, 90})
        menu.addAction(QStringLiteral("%1 minutes").arg(minutes), this, [this, minutes] {
            m_sleepAtSongEnd = false;
            m_sleep.start(minutes * 60 * 1000);
            toast(QStringLiteral("Pausing in %1 minutes").arg(minutes));
        });
    menu.addAction("End of this song", this, [this] {
        m_sleep.stop();
        m_sleepAtSongEnd = true;
        m_sleepSong = m_mpv.property("path").toString() + QString::number(m_mpv.property("playlist-pos").toInt());
        toast("Pausing when this song ends");
    });
    if (on) {
        menu.addSeparator();
        menu.addAction("Turn off the sleep timer", this, [this] {
            m_sleep.stop();
            m_sleepAtSongEnd = false;
            toast("Sleep timer off");
        });
    }
    menu.exec(global);
}

void MainWindow::applyEqualizer(int band)
{
    if (!m_mpv.connected())
        return;
    // Measured on mpv 0.41 with a tone playing (2026-11-12): adding,
    // rebuilding and removing the filter moved the playback clock no more
    // than doing nothing did (+-40 ms of polling), and af-command changes a
    // band in place, so a slider is followed as it moves.
    const QVector<int> gains = equalizer::gains();
    if (!equalizer::enabled()) {
        m_mpv.command({"af", "remove", "@ghosteq"});
        return;
    }
    if (band >= 0 && band < equalizer::kBands) {
        m_mpv.command({"af-command", "ghosteq", "g", QString::number(gains[band]), QStringLiteral("equalizer@b%1").arg(band)});
        int most = 0;
        for (int g : gains)
            most = qMax(most, g);
        m_mpv.command({"af-command", "ghosteq", "volume", QStringLiteral("%1dB").arg(-most), "volume@pre"});
        return;
    }
    // First in the chain, so Normalize (added at its end) evens out what
    // the equalizer made.
    m_mpv.command({"af", "remove", "@ghosteq"});
    m_mpv.command({"af", "pre", QStringLiteral("@ghosteq:") + equalizer::filter(gains)});
}

void MainWindow::importSpotify()
{
    SpotifyImportDialog dialog(this);
    dialog.onImported = [this](const QString &name, const QVector<spotify::Track> &tracks) {
        // As the bot writes a Spotify list: each song a YouTube search,
        // found when it plays (songmatch picks the right upload).
        const QString path = m_library.createPlaylist(name);
        if (path.isEmpty()) {
            QMessageBox::warning(this, "Import from Spotify", "The playlist couldn't be made.");
            return;
        }
        QVector<PlaylistEntry> entries;
        for (const spotify::Track &t : tracks)
            entries << PlaylistEntry{t.artist.isEmpty() ? t.title : t.artist + QStringLiteral(" - ") + t.title,
                                     QStringLiteral("ytdl://ytsearch1:%1 %2").arg(t.title, t.artist).trimmed(), {}};
        if (!Playlist::append(path, entries)) {
            QMessageBox::warning(this, "Import from Spotify", "The playlist file couldn't be written.");
            return;
        }
        toast(QStringLiteral("Imported %1 songs: %2").arg(tracks.size()).arg(name));
        reopenAfter(path);
    };
    dialog.exec();
}
