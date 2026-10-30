#include "mainwindow.h"

#include "covercrop.h"
#include "settings.h"
#include "songinfo.h"
#include "videopage.h"
#include "icons.h"
#include "nowplaying.h"
#include "theme.h"
#include "tiles.h"

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
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <numeric>
#include <random>

#include <windows.h>
#include <dwmapi.h>

namespace {

constexpr int kTile = 168;          // cover size on the Home shelves
constexpr int kShelfCount = 8;      // tiles on each Home shelf
constexpr int kMinutesPerSong = 3;  // for the "hours of music" estimate
constexpr int kTopBar = 50;         // height of the draggable strip: 8 px under the search box
constexpr int kHeaderTall = 264;    // playlist header, scrolled to the top
constexpr int kHeaderShort = 92;    // ... and once the songs are scrolled
constexpr int kCompactWidth = 84;   // the library panel, covers only
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

// Wheel scrolling that glides instead of jumping three rows a notch: each
// notch moves the target 110 px and the view eases to it, as Spotify's does.
class SmoothScroll : public QObject {
public:
    explicit SmoothScroll(QAbstractScrollArea *area) : QObject(area), m_bar(area->verticalScrollBar())
    {
        area->viewport()->installEventFilter(this);
        m_anim.setDuration(220);
        m_anim.setEasingCurve(QEasingCurve::OutCubic);
        connect(&m_anim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &v) { m_bar->setValue(v.toInt()); });
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() != QEvent::Wheel)
            return false;
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (wheel->modifiers() != Qt::NoModifier || wheel->angleDelta().y() == 0)
            return false;
        const int from = m_anim.state() == QAbstractAnimation::Running ? m_target : m_bar->value();
        m_target = qBound(m_bar->minimum(), from - wheel->angleDelta().y() * 110 / 120, m_bar->maximum());
        m_anim.stop();
        m_anim.setStartValue(m_bar->value());
        m_anim.setEndValue(m_target);
        m_anim.start();
        return true;
    }

private:
    QScrollBar *m_bar;
    QVariantAnimation m_anim;
    int m_target = 0;
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

// The top of a playlist page: its collage, and behind it a wash of the
// covers' own colour fading into the page, the way Spotify tints a playlist.
class PlaylistHeader : public QWidget {
public:
    using Source = std::function<QVector<QPixmap>()>;
    explicit PlaylistHeader(Source source) : m_source(std::move(source)) {}
    void setName(const QString &name) { m_name = name; update(); }
    // A colour the user chose for this list; invalid follows the cover.
    void setTint(const QColor &tint) { m_tint = tint; update(); }
    // The cover fills the header's height less its margins, so shrinking the
    // header on scroll shrinks the cover with it.
    QRect coverRect() const
    {
        const int margin = height() > 160 ? 28 : 14;
        const int side = height() - 2 * margin;
        return {margin + 4, margin, side, side};
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QVector<QPixmap> covers = m_source();
        QColor tint = theme::placeholderFor(m_name);
        if (m_tint.isValid()) {
            tint = m_tint;
        } else if (!covers.isEmpty() && !covers.first().isNull()) {
            // The first cover's average colour, kept dark enough for white
            // text on top.
            const QColor avg = covers.first().toImage()
                .scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixelColor(0, 0);
            tint = QColor::fromHsv(avg.hsvHue(), qMin(avg.hsvSaturation(), 150), qBound(70, avg.value(), 120));
        }
        QLinearGradient wash(0, 0, 0, height());
        wash.setColorAt(0, tint);
        wash.setColorAt(1, QColor(22, 21, 27, 0));
        QPainterPath top;
        top.addRoundedRect(QRectF(rect()).adjusted(0, 0, 0, 20), 12, 12);
        p.fillPath(top, wash);
        // A soft shadow under the cover lifts it off the wash.
        const QRect c = coverRect();
        for (int i = 1; i <= 6; ++i) {
            QPainterPath shadow;
            const qreal round = tiles::playlistRadius(QRectF(c), 10) + i;
            shadow.addRoundedRect(QRectF(c).adjusted(-i, -i + 4, i, i + 4), round, round);
            p.fillPath(shadow, QColor(0, 0, 0, 14));
        }
        tiles::paintCollage(p, c, covers, m_name, 10);
    }

private:
    Source m_source;
    QString m_name;
    QColor m_tint;
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
    m_pages->addWidget(buildPlaylistPage());
    m_settings = new SettingsPage;
    m_pages->addWidget(m_settings);
    m_lyricsView = new LyricsView;
    m_pages->addWidget(m_lyricsView);
    m_videoPage = new VideoPage;
    m_pages->addWidget(m_videoPage);
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
        // The entry's place in mpv's list, which a shuffle may have moved.
        const QVariantList list = m_mpv.property("playlist").toList();
        for (int i = 0; i < list.size(); ++i)
            if (list[i].toMap().value("filename").toString() == target) {
                m_mpv.command({"playlist-play-index", i});
                m_mpv.setProperty("pause", false);
                return;
            }
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
    m_videoPage->onFullScreen = [this] { setVideoFullScreen(!m_videoFull); };
    m_videoPage->onTogglePause = [this] { m_mpv.command({"cycle", "pause"}); };
    m_videoPage->onPrevious = [this] { m_mpv.command({"playlist-prev"}); };
    m_videoPage->onNext = [this] { m_mpv.command({"playlist-next"}); };
    m_videoPage->onVolume = [this](int v) { m_mpv.setProperty("volume", v); };
    m_videoPage->onToggleMute = [this] { m_mpv.command({"cycle", "mute"}); };
    m_videoPage->onScreenMoved = [this] { schedulePlace(); };
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
    m_videoPage->onSkip = [this](double s) { m_mpv.command({"seek", s}); };
    m_videoPage->onSubtitle = [this](const QVariant &sid) { m_mpv.setProperty("sid", sid); };
    m_videoPage->onSpeed = [this](double s) { m_mpv.setProperty("speed", s); };
    m_videoPage->onQuality = [this](int height) {
        // The cap for this and the next videos, this one reopened at the
        // same second (quality.lua does the same when it steps down).
        const QString format = height > 0 ? QStringLiteral("bestvideo[height<=%1]+bestaudio/best").arg(height)
                                          : QStringLiteral("bestvideo+bestaudio/best");
        m_mpv.setProperty("ytdl-format", format);
        m_mpv.setProperty("start", QString::number(m_mpv.property("time-pos").toDouble(), 'f', 1));
        m_resetStart = true;
        m_mpv.command({"playlist-play-index", m_mpv.property("playlist-pos").toInt()});
    };
    // On the video page the keys mpv would take in its own window.
    auto key = [this](const QKeySequence &seq, std::function<void()> act) {
        auto *s = new QShortcut(seq, m_videoPage);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, act);
    };
    key(QKeySequence(Qt::Key_Space), [this] { m_mpv.command({"cycle", "pause"}); });
    key(QKeySequence(Qt::Key_Left), [this] { m_mpv.command({"seek", -10}); });
    key(QKeySequence(Qt::Key_Right), [this] { m_mpv.command({"seek", 10}); });
    key(QKeySequence(Qt::Key_F), [this] { setVideoFullScreen(!m_videoFull); });
    key(QKeySequence(Qt::Key_I), [this] { if (m_videoPage->onMinimize) m_videoPage->onMinimize(); });
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
    auto *mainBox = new QVBoxLayout(mainPanel);
    mainBox->setContentsMargins(0, 0, 0, 0);
    mainBox->addWidget(m_pages);

    // The now playing view, Spotify's, at the right of the page: a panel of
    // its own, its width the user's.
    m_panel = new NowPlayingPanel;
    m_rightSplit = new QSplitter(Qt::Horizontal);
    m_rightSplit->addWidget(mainPanel);
    m_rightSplit->addWidget(m_panel);
    m_rightSplit->setStretchFactor(0, 1);
    m_rightSplit->setStretchFactor(1, 0);
    m_rightSplit->setHandleWidth(8);
    m_rightSplit->setChildrenCollapsible(false);
    m_rightSplit->setSizes({940, 340});
    m_panelOpen = QSettings().value("panel/open", true).toBool();
    m_panel->onClose = [this] {
        m_panelOpen = false;
        QSettings().setValue("panel/open", false);
        updatePanelVisibility();
    };
    m_panel->onSwitchVideo = [this] { setSongVideo(!songVideo()); };
    m_panel->onOpenLyrics = [this] {
        if (m_pages->currentWidget() != m_lyricsView)
            toggleLyrics();
    };
    m_panel->onAbout = [this] {
        const QString source = m_mpv.property("user-data/ghost/source").toString();
        const auto [artist, track] = playingSong();
        showSongInfo(source.isEmpty() ? m_mpv.property("path").toString() : source,
                     track.isEmpty() ? m_mpv.property("media-title").toString() : track, artist);
    };
    m_panel->onPlayNext = [this] { m_mpv.command({"playlist-next"}); };
    connect(&m_artists, &ArtistLookup::ready, this, [this](const QString &channel) {
        if (channel == m_panelChannel)
            m_panel->setArtist(m_artists.known(channel));
    });
    m_panelSoon.setSingleShot(true);
    m_panelSoon.setInterval(150);
    connect(&m_panelSoon, &QTimer::timeout, this, &MainWindow::refreshPanel);
    m_videoPage->onSwitchToAudio = [this] { setSongVideo(false); };

    m_split = new QSplitter(Qt::Horizontal);
    m_split->addWidget(buildLibrary());
    m_split->addWidget(m_rightSplit);
    m_split->setStretchFactor(1, 1);
    m_split->setHandleWidth(8);
    m_split->setChildrenCollapsible(false);
    m_split->setSizes({300, 940});
    connect(m_split, &QSplitter::splitterMoved, this, [this] {
        setLibraryCompact(m_split->sizes().value(0) < kCompactBelow);
        syncSearchWidth();
        m_splitMoved.start();
        // The controls over the picture follow the edge at once, from the
        // page as it is laid out now.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        placeSurface();
    });
    connect(m_rightSplit, &QSplitter::splitterMoved, this, [this] {
        m_splitMoved.start();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        placeSurface();
    });

    m_nowPlaying = new NowPlayingBar(&m_mpv, &m_covers);
    m_settings->onLookChanged = [this] { applyLook(); };
    m_settings->downloadsSummary = [this] {
        const int n = m_offline.songCount();
        return n == 0 ? QStringLiteral("Nothing downloaded yet.")
                      : QStringLiteral("%1 %2, %3 MB").arg(n).arg(n == 1 ? "song" : "songs")
                            .arg(double(m_offline.bytesUsed()) / (1024 * 1024), 0, 'f', 1);
    };
    m_settings->onRemoveDownloads = [this] { m_offline.removeAll(); };
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
    });

    auto *root = new QVBoxLayout;
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildTopBar());
    // The same gap on every side of the two panels.
    auto *body = new QHBoxLayout;
    body->setContentsMargins(8, 0, 8, 0);
    body->addWidget(m_split);
    root->addLayout(body, 1);
    root->addWidget(m_nowPlaying);
    auto *central = new QWidget;
    central->setObjectName("central");
    central->setLayout(root);
    setCentralWidget(central);

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

    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    m_split->restoreState(settings.value("window/split").toByteArray());
    m_rightSplit->restoreState(settings.value("window/panelSplit").toByteArray());
    setLibraryCompact(m_split->sizes().value(0) < kCompactBelow);

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
        for (QListWidget *v : {m_libraryList, m_recentAdded, m_recentPlayed, m_songs})
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
    connect(&m_covers, &ThumbnailCache::ready, this, [this] { if (m_panel->isVisible()) m_panelSoon.start(); });
    connect(&m_mpv, &MpvClient::propertyChanged, this, [this, repaint](const QString &name) {
        // A song played since the last look may have given a list its cover.
        if (name == "user-data/ghost/source")
            QTimer::singleShot(500, repaint);
        if (name == "playlist-pos" || name == "playlist-path" || name == "path")
            markPlaying();
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
        if (name == "user-data/ghost/audio")
            schedulePlace();
        if (name == "path" || name == "user-data/ghost/source" || name == "media-title" || name == "playlist-pos" ||
            name == "playlist" || name == "playlist-path" || name == "user-data/ghost/audio" ||
            name == "user-data/ghost/video-on" || name == "metadata/by-key/uploader")
            m_panelSoon.start();
        if (name == "time-pos" && m_panel->isVisible())
            m_panel->setPosition(m_mpv.property("time-pos").toDouble());
        if (name == "user-data/ghost/video-on")
            songVideoChanged();
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
        // The video page's own controls.
        if (m_pages->currentWidget() == m_videoPage) {
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
        if (QSettings().value("library/autoImport").toBool())
            importStray(false);
        rescan();
    });

    qApp->installEventFilter(this);
    for (auto [keys, step] : {std::pair{QKeySequence::ZoomIn, 0.1}, std::pair{QKeySequence::ZoomOut, -0.1}}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, this, [this, step] { changeZoom(step); });
    }
    connect(new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this), &QShortcut::activated, this, [this] { changeZoom(0); });
    const qreal savedZoom = QSettings().value("window/zoom", 1.0).toReal();
    if (!qFuzzyCompare(savedZoom, 1.0))
        QTimer::singleShot(0, this, [this, savedZoom] { changeZoom(savedZoom - theme::zoom()); });

    applyLook();
    connect(&m_mpv, &MpvClient::connectionChanged, this, [this](bool on) {
        schedulePlace();
        if (!on) {
            m_mpvEmbedded = false;
            if (m_restartPlay) {
                const QueuedPlay q = *m_restartPlay;
                m_restartPlay.reset();
                QTimer::singleShot(200, this, [this, q] { play(q.path, q.shuffle, q.startAt, q.order); });
            }
            return;
        }
        m_mpvEmbedded = m_launching.isValid() && m_launchEmbedded;
        m_keepAspect = -1;
        sendWindowChoice();
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
    if (m_mpvEmbedded && m_mpv.connected()) {
        m_mpv.command({"quit"});
        m_mpv.flush();
    }
    if (!QSettings().value("player/window", true).toBool() && m_mpv.connected() &&
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
    if (event->type() == QEvent::WindowStateChange)
        schedulePlace();   // the windows over the picture go and come with it
    QMainWindow::changeEvent(event);
}

QWidget *MainWindow::buildTopBar()
{
    auto *home = glyphButton(kIconHome, "Home", "pill");
    connect(home, &QPushButton::clicked, this, &MainWindow::showHome);

    m_search = new QLineEdit;
    m_search->setObjectName("topSearch");
    m_search->setPlaceholderText("Search your library");
    m_search->setClearButtonEnabled(true);
    m_search->setFixedWidth(360);
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::refill);

    auto *minimize = glyphButton(kIconMin, "Minimize", "chrome");
    m_maxButton = glyphButton(kIconMax, "Maximize", "chrome");
    auto *close = glyphButton(kIconClose, "Close", "chromeClose");
    auto *gear = glyphButton(QChar(0xE713), "Settings", "pill");
    connect(gear, &QPushButton::clicked, this, &MainWindow::showSettings);
    connect(minimize, &QPushButton::clicked, this, &MainWindow::showMinimized);
    // Through Windows itself rather than Qt's showMaximized(): Qt resizes a
    // frameless window to the screen, so Windows took the full size for the
    // normal one, and dragging the window off maximised kept it full size.
    connect(m_maxButton, &QPushButton::clicked, this, [this] {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    });
    connect(close, &QPushButton::clicked, this, &MainWindow::close);
    m_chromeButtons = {home, m_search, gear, minimize, m_maxButton, close};

    m_topBar = new QWidget;
    m_topBar->setObjectName("topBar");
    m_topBar->setAttribute(Qt::WA_StyledBackground);
    m_topBar->setFixedHeight(kTopBar);
    auto *row = new QHBoxLayout(m_topBar);
    row->setContentsMargins(8, 0, 0, 0);
    row->setSpacing(8);
    row->addWidget(home);
    row->addWidget(m_search);
    row->addStretch();
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
    m_collapse = glyphButton(kIconCollapse, "Collapse library");
    connect(m_collapse, &QPushButton::clicked, this, [this] {
        const bool compact = !m_compact;
        const int total = m_split->sizes().value(0) + m_split->sizes().value(1);
        const int left = compact ? kCompactWidth : 300;
        m_split->setSizes({left, total - left});
        setLibraryCompact(compact);
    });
    auto *add = glyphButton(kIconAdd, "New folder, add a folder, refresh");
    m_addButton = add;
    connect(add, &QPushButton::clicked, this, [this, add] { showLibraryMenu(add->mapToGlobal(QPoint(0, add->height()))); });
    // Covers or one line per list, as Spotify's "Compact" view.
    m_viewToggle = glyphButton(kIconDense, "Compact list");
    connect(m_viewToggle, &QPushButton::clicked, this, [this] { setLibraryDense(!m_rowDelegate->dense()); });

    m_libraryHead = new QWidget;
    auto *head = new QHBoxLayout(m_libraryHead);
    head->setContentsMargins(0, 0, 0, 0);
    head->addWidget(m_collapse);
    head->addWidget(m_libraryTitle, 1);
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
    m_libraryList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_rowDelegate = new tiles::RowDelegate([this](const QModelIndex &index) {
        return coversFor(index.data(tiles::kIndexRole).toInt());
    }, m_libraryList);
    m_libraryList->setItemDelegate(m_rowDelegate);
    connect(m_libraryList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString folder = item->data(tiles::kFolderRole).toString();
        if (folder.isEmpty()) {
            openPlaylist(item->data(tiles::kIndexRole).toInt());
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
    panel->setMinimumWidth(kCompactWidth);
    // Compact or not follows the panel's real width, judged whenever it
    // changes: read in the constructor it was not laid out yet, looked
    // narrow, and the panel opened compact at full width.
    panel->installEventFilter(this);
    m_libraryPanel = panel;
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(10, 14, 6, 10);
    box->setSpacing(10);
    box->addWidget(m_libraryHead);
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
    m_collapse->setText(QString(compact ? kIconExpand : kIconCollapse));
    m_collapse->setToolTip(compact ? "Expand library" : "Collapse library");
    m_rowDelegate->setCompact(compact);
    // The video page with the library folded away: a cinema.
    if (m_videoPage)
        m_videoPage->setCinema(compact);
    // A scroll bar in a cover-wide panel would cut the covers off.
    m_libraryList->setVerticalScrollBarPolicy(compact ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    m_libraryList->doItemsLayout();
}

void MainWindow::showLibraryMenu(const QPoint &at)
{
    QMenu menu(this);
    menu.addAction("New playlist...", this, [this] { newPlaylist(); });
    menu.addAction("New folder...", this, [this] { newFolder(); });
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

    auto *page = new QWidget;
    page->setObjectName("home");
    auto *box = new QVBoxLayout(page);
    box->setContentsMargins(28, 22, 28, 16);
    box->setSpacing(12);
    box->addWidget(m_greeting);
    box->addSpacing(4);
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
    header->setFixedHeight(kHeaderTall);
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
    text->addWidget(m_playlistInfo);
    m_headRow = new QHBoxLayout(header);
    m_headRow->addLayout(text, 1);

    auto *playButton = new icons::IconButton(icons::Icon::Play, 56, "Play");
    playButton->setRound(theme::accent(), theme::accentText(), QColor(24, 18, 34));
    auto *shuffleButton = new icons::IconButton(icons::Icon::Shuffle, 40, "Shuffle");
    auto *folderButton = new icons::IconButton(icons::Icon::Folder, 40, "Show in folder");
    connect(playButton, &QAbstractButton::clicked, this, [this] { playOpen(0); });
    shuffleButton->setToolTip("Shuffle the order of this list");
    m_pageShuffle = shuffleButton;
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
    connect(m_songs->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        const qreal t = qBound(0.0, value / 170.0, 1.0);
        m_playlistCover->setFixedHeight(int(kHeaderTall - t * (kHeaderTall - kHeaderShort)));
        layoutHeader();
    });
    connect(m_songs, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
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
        }
        QMenu *to = menu.addMenu(count > 1 ? QStringLiteral("Add %1 songs to playlist").arg(count) : QStringLiteral("Add to playlist"));
        to->addAction("New playlist...", this, [this] { newPlaylist(QString(), true); });
        to->addSeparator();
        const QString here = m_library.playlists().value(m_open).path;
        const QString home = Library::homeFolder();
        for (const Playlist &p : m_library.playlists()) {
            const QString dir = QDir::cleanPath(QFileInfo(p.path).absolutePath());
            const bool ours = dir.compare(home, Qt::CaseInsensitive) == 0 || dir.startsWith(home + '/', Qt::CaseInsensitive);
            if (p.path == here || !ours)
                continue;   // lists outside the library are the bot's to write
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

    auto *page = new QWidget;
    auto *box = new QVBoxLayout(page);
    box->setContentsMargins(0, 0, 0, 6);
    box->setSpacing(8);
    box->addWidget(header);
    box->addLayout(actions);
    box->addLayout(list, 1);
    return page;
}

void MainWindow::layoutHeader()
{
    if (!m_playlistCover || !m_headRow)
        return;   // a resize while the window is still being built
    auto *header = static_cast<PlaylistHeader *>(m_playlistCover);
    const QRect cover = header->coverRect();
    const bool tall = header->height() > 160;
    m_headRow->setContentsMargins(cover.right() + (tall ? 26 : 16), cover.top(), 28, cover.top());
    m_playlistKind->setVisible(tall);
    m_playlistInfo->setVisible(header->height() > 120);
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
    const qreal t = qBound(0.0, qreal(kHeaderTall - header->height()) / (kHeaderTall - kHeaderShort), 1.0);
    qreal size = 40 - t * 20;
    QFont font = m_playlistName->font();
    font.setWeight(QFont::ExtraBold);
    for (; size > 18; size -= 1) {
        font.setPointSizeF(size);
        if (QFontMetrics(font).horizontalAdvance(name) <= available)
            break;
    }
    font.setPointSizeF(size);
    m_titlePt = size;
    const bool renaming = m_nameEdit && m_nameEdit->isVisible();
    m_playlistName->setStyleSheet(titleSheet(size, renaming));
    m_playlistName->setText(QFontMetrics(font).elidedText(name, Qt::ElideRight, qMax(40, available)));
    if (renaming) {
        // Narrowed or widened with the panels, as the songs below are.
        m_nameEdit->setStyleSheet(QStringLiteral("font-size: %1pt;").arg(size));
        m_nameEdit->setGeometry(m_playlistName->geometry().adjusted(-8, -4, 8, 4));
    }
}

QString MainWindow::titleSheet(qreal points, bool hidden)
{
    // The size is the title's own; its tooltip ("Double-click to rename")
    // would otherwise take it too and show at 40 points.
    return QStringLiteral("QLabel { font-size: %1pt; %2 } QToolTip { font-size: 9pt; font-weight: 400; color: rgb(236,236,241); }")
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
    m_nameEdit->setGeometry(m_playlistName->geometry().adjusted(-8, -4, 8, 4));
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
            QStringLiteral("There are %1 playlists from the bot in your Downloads folder.\n\n"
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
    if (dragOrResize(watched, event))
        return true;
    if (watched == m_pages && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        schedulePlace();
    if (watched == m_libraryPanel && event->type() == QEvent::Resize && m_rowDelegate && !m_videoFull) {
        const bool compact = m_libraryPanel->width() < kCompactBelow;
        if (compact != m_compact)
            setLibraryCompact(compact);
    }
    if (watched == m_nameEdit && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        m_renameCancelled = true;
        finishRename();
        return true;
    }
    // Ctrl + wheel zooms, as in a browser.
    if (event->type() == QEvent::Wheel && (static_cast<QWheelEvent *>(event)->modifiers() & Qt::ControlModifier)) {
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
    if (m_songs && watched == m_songs->viewport() && event->type() == QEvent::MouseButtonRelease && !m_videoGrid) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item = m_songs->itemAt(mouse->position().toPoint());
        if (item && mouse->button() == Qt::LeftButton && mouse->position().x() < 48 && m_open >= 0) {
            playOpen(m_songs->row(item));
            return true;
        }
    }
    if (watched->objectName() == "clockHeader" && event->type() == QEvent::Paint) {
        auto *w = static_cast<QPushButton *>(watched);
        QPainter p(w);
        const bool active = m_sortKey == SortKey::Duration;
        const QColor ink = active || w->underMouse() ? QColor(236, 236, 241) : theme::muted();
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
    restoreLibraryWidth();
    layoutHeader();
    syncSearchWidth();
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
}

void MainWindow::syncSearchWidth()
{
    // The search box spans the library panel below it, edge to edge.
    if (!m_search || !m_split)
        return;
    const int panel = m_split->sizes().value(0);
    m_search->setFixedWidth(qMax(220, panel - 42));   // less the Home button and its gap
}

void MainWindow::openPlayingList()
{
    // A video list goes to its watch page, also while mpv plays it in its own
    // window: that page is where it is brought back from.
    if ((m_mpvEmbedded || m_videoPoppedOut) && m_mpv.connected() &&
        (!m_mpv.property("user-data/ghost/audio").toBool() || songVideo())) {
        showVideoPage();
        return;
    }
    const QString playing = playingListPath();
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
    for (QListWidget *v : {m_libraryList, m_recentAdded, m_recentPlayed})
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
    for (QListWidget *v : {m_libraryList, m_recentAdded, m_recentPlayed})
        v->viewport()->update();
    m_playlistCover->update();
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
    theme::setZoom(*static_cast<QApplication *>(QCoreApplication::instance()), step == 0 ? 1.0 : theme::zoom() + step);
    QSettings().setValue("window/zoom", theme::zoom());
    for (QListWidget *v : {m_libraryList, m_recentAdded, m_recentPlayed, m_songs})
        v->doItemsLayout();
    for (QListWidget *v : {m_recentAdded, m_recentPlayed})
        v->setFixedHeight(int((kTile + 70 + 26) * theme::zoom()));
    layoutHeader();
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
    for (QListWidget *v : {m_libraryList, m_recentAdded, m_recentPlayed})
        v->viewport()->update();
    m_playlistCover->update();
}

void MainWindow::sendWindowChoice()
{
    // Only to an mpv the app talks to: an mpv opened from Explorer keeps
    // its window, so a song list there can always be stopped.
    if (m_mpv.connected())
        m_mpv.command({"change-list", "script-opts", "append",
                       QSettings().value("player/window", true).toBool() ? "ghost-window=yes" : "ghost-window=no"});
}

void MainWindow::showHome()
{
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
    const Playlist &p = all[index];
    m_playlistKind->setText(p.audio ? "PLAYLIST  ·  SONGS" : "PLAYLIST  ·  VIDEOS");
    m_playlistInfo->setText(subtitleFor(p) + QStringLiteral("  ·  added ") +
                            QLocale().toString(p.modified.date(), QLocale::ShortFormat));
    static_cast<PlaylistHeader *>(m_playlistCover)->setName(p.name);
    static_cast<PlaylistHeader *>(m_playlistCover)->setTint(
        QSettings().value("headerColor/" + QFileInfo(p.path).fileName()).value<QColor>());

    setVideoGrid(!p.audio);
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
    applySort();
    m_emptyHint->setVisible(m_songs->count() == 0);
    showDownloadState();
    m_pages->setCurrentIndex(1);
    if (keepView) {
        m_songs->verticalScrollBar()->setValue(scrolled);
    } else {
        m_songs->scrollToTop();
        m_playlistCover->setFixedHeight(kHeaderTall);
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
        b->setColors(on ? theme::accent() : QColor(170, 168, 182), on ? theme::accent().lighter(120) : Qt::white);
        b->setDot(on);
    }
    filterSongs();
    markPlaying();
}

void MainWindow::filterSongs()
{
    const QString needle = m_songSearch->text().trimmed();
    for (int row = 0; row < m_songs->count(); ++row) {
        QListWidgetItem *item = m_songs->item(row);
        item->setHidden(!needle.isEmpty() &&
            !item->text().contains(needle, Qt::CaseInsensitive) &&
            !item->data(tiles::kArtistRole).toString().contains(needle, Qt::CaseInsensitive));
    }
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

void MainWindow::newPlaylist(const QString &folder, bool addSelected)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New playlist", "Playlist name:", QLineEdit::Normal,
                                               QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    const QString path = m_library.createPlaylist(name, folder);
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

void MainWindow::toast(const QString &text)
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
    m_toastTimer.start(2600);
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
    if (m_open < 0)
        return;
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
    m_downloadButton->setStyleSheet(marked ? QStringLiteral("color: rgb(176,127,224);") : QString());
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
    m_chips->button(AllLists)->setText(QStringLiteral("All %1").arg(all.size()));
    m_chips->button(Songs)->setText(QStringLiteral("Songs %1").arg(songLists));
    m_chips->button(Videos)->setText(QStringLiteral("Videos %1").arg(all.size() - songLists));

    // library panel
    const QString needle = m_search->text().trimmed();
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
        for (int i = 0; i < all.size(); ++i)
            if (shown(all[i]))
                addList(i, false);
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

    // Home
    m_greeting->setText(greetingFor(QTime::currentTime()));
    m_statLists->setText(QString::number(all.size()));
    m_statSongs->setText(QString::number(items));
    m_statHours->setText(QString::number((items * kMinutesPerSong + 30) / 60));
    m_statNew->setText(QString::number(fresh));

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
        return {};
    const QFileInfo info(mpvSays);
    if (info.exists())
        return info.absoluteFilePath();
    for (const Playlist &p : m_library.playlists())
        if (QFileInfo(p.path).fileName().compare(info.fileName(), Qt::CaseInsensitive) == 0)
            return QFileInfo(p.path).absoluteFilePath();
    return info.absoluteFilePath();
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
    m_pendingPath = path;
    m_pendingOrder = order;
    // A list that never loads should not reorder a later one.
    const int serial = ++m_pendingSerial;
    if (!order.isEmpty())
        QTimer::singleShot(15000, this, [this, serial] { if (m_pendingSerial == serial) m_pendingOrder.clear(); });
    // An mpv already open takes the list over IPC, so a second window does
    // not start on top of the first.
    if (!m_mpv.connected() && m_launching.isValid() && m_launching.elapsed() < 15000) {
        m_queuedPlay = QueuedPlay{path, shuffle, startAt, order};   // the latest click wins
        return;
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
        m_mpv.command({"loadfile", path, "replace", -1, QStringLiteral("playlist-start=%1").arg(startAt)});
        if (shuffle)
            QTimer::singleShot(400, this, [this] { m_mpv.command({"playlist-shuffle"}); });
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
    QStringList args{QStringLiteral("--input-ipc-server=%1").arg(MpvClient::kPipe)};
    if (!QSettings().value("player/window", true).toBool())
        args << QStringLiteral("--script-opts-append=ghost-window=no");
    if (shuffle)
        args << "--shuffle";
    if (startAt > 0)
        args << QStringLiteral("--playlist-start=%1").arg(startAt);
    if (m_startSeconds > 1)
        args << QStringLiteral("--start=%1").arg(m_startSeconds, 0, 'f', 1);
    m_startSeconds = 0;
    // Inside the app whenever videos may show there, so a video list later
    // does not need mpv started again. Keys go to the app (it passes them
    // on), mouse and mpv's own controls stay mpv's.
    m_launchEmbedded = embed == 1 || (embed == -1 && embedFor(false) == 1);
    if (m_launchEmbedded)
        args << QStringLiteral("--wid=%1").arg(qulonglong(m_videoPage->surface()->winId()))
             << "--input-vo-keyboard=no";
    args << "--" << path;
    // From mpv's own folder, the way a double-click on the .m3u starts it, so
    // its scripts and the presence agent behave the same.
    qint64 pid = 0;
    if (QProcess::startDetached(mpv, args, QFileInfo(mpv).absolutePath(), &pid)) {
        m_launchedPids << pid;
        m_launching.start();
        m_mpv.hurry();
        if (!audio && m_launchEmbedded)
            showVideoPage();
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
    return QSettings().value("player/window", true).toBool() ? 0 : -1;
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
    m_videoPage->setCinema(m_compact);
    m_miniClosed = false;
    if (m_pages->currentWidget() != m_videoPage)
        m_beforeVideo = m_pages->currentWidget();
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
        m_songs->setResizeMode(grid ? QListView::Adjust : QListView::Fixed);
        if (!grid)
            m_songs->setGridSize(QSize());
        m_columnsBar->setVisible(!grid);
    }
    fitVideoGrid();
}

void MainWindow::fitVideoGrid()
{
    // As many columns as fit at 280 px or more, sharing the width.
    if (!m_videoGrid)
        return;
    const int width = m_songs->viewport()->width() - 4;
    const int columns = qMax(1, width / int(280 * theme::zoom()));
    m_cardDelegate->setCardWidth(width / columns);
    if (m_songs->gridSize() != m_cardDelegate->cardSize())
        m_songs->setGridSize(m_cardDelegate->cardSize());
}

bool MainWindow::videoShowing() const
{
    return m_mpvEmbedded && m_mpv.connected() && !m_videoPoppedOut &&
           (!m_mpv.property("user-data/ghost/audio").toBool() || songVideo());
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
    const bool mini = video && !onPage && !m_miniClosed;
    QWidget *miniWindow = m_videoPage->miniWindow();
    QRect visible;   // of the picture, in the host's coordinates
    if (onPage) {
        // The page scrolls the picture up and out of view: only what is
        // inside the page's scrolling area shows.
        QWidget *screen = m_videoPage->screen();
        m_videoPage->setSurfaceCorner(m_videoFull ? 0 : 12);
        const QRect picture(host->mapFromGlobal(screen->mapToGlobal(QPoint())), screen->size());
        const QRect view(host->mapFromGlobal(m_videoPage->viewport()->mapToGlobal(QPoint())), m_videoPage->viewport()->size());
        visible = picture & view;
        m_videoPage->setSurfaceVisibleArea(visible.isEmpty() ? QRect(0, 0, 1, 1) : visible.translated(-picture.topLeft()));
        surface->place(this, picture);
        miniWindow->hide();
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
        miniWindow->show();
        surface->place(miniWindow, QRect(QPoint(0, 0), miniWindow->size()));
    } else {
        miniWindow->hide();
    }
    surface->setVisible(onPage || mini);
    m_surfaceOnPage = onPage;

    // mpv fills its window when the window already has the video's shape,
    // so no hairline of black is left at its edges; in full screen it keeps
    // the shape and draws its black around it.
    if (m_mpv.connected() && m_mpvEmbedded && (onPage || mini)) {
        const int keep = onPage && m_videoFull ? 1 : 0;
        if (keep != m_keepAspect) {
            m_keepAspect = keep;
            m_mpv.setProperty("keepaspect", bool(keep));
        }
    }

    const QRect global(host->mapToGlobal(surface->geometry().topLeft()), surface->geometry().size());
    m_videoArea = onPage ? QRect(host->mapToGlobal(visible.topLeft()), visible.size()) : QRect();
    if (onPage)
        m_videoPage->placeOverPicture(global, m_videoArea);
    else
        m_videoPage->corners()->hide();
    // The controls sit on the picture's bottom edge, so only while that is
    // in view.
    QWidget *controls = m_videoPage->controls();
    m_barFits = onPage && !m_videoArea.isEmpty() && m_videoArea.bottom() >= global.bottom() - 1;
    if (m_barFits) {
        const int h = controls->sizeHint().height();
        controls->setGeometry(global.left(), global.bottom() + 1 - h, global.width(), h);
        if (controls->isVisible())
            controls->raise();
    } else {
        controls->hide();
    }
    // The song bar is for songs: on the video page the controls over the
    // picture stand in for it.
    m_nowPlaying->setVisible(!m_videoFull && !(m_pages->currentWidget() == m_videoPage && !m_videoPoppedOut));
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
    if (!m_surfaceOnPage || !m_barFits) {
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
    if (!m_videoPage || (m_mpv.property("user-data/ghost/audio").toBool() && !songVideo()))
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
    }
    const QString path = m_mpv.property("path").toString();
    QString id = youtube::videoId(m_mpv.property("user-data/ghost/source").toString());
    if (id.isEmpty())
        id = youtube::videoId(path);
    m_videoPage->setPlaying(path, m_mpv.property("media-title").toString(),
                            m_mpv.property("metadata/by-key/uploader").toString(),
                            id.isEmpty() ? QString() : QStringLiteral("https://www.youtube.com/watch?v=") + id);
    if (!id.isEmpty())
        describeVideo(id);
}

void MainWindow::describeVideo(const QString &id)
{
    // Only for videos, only once each: one yt-dlp call that downloads
    // nothing, after the video has started, so it never delays playing.
    // The first line yt-dlp prints is "views|upload date", the rest is the
    // description.
    auto show = [this](const QString &printed) {
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
    };
    if (m_descriptions.contains(id)) {
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
        if (now == id)
            show(text);
    });
    proc->start(QFileInfo(findMpv()).absoluteDir().filePath("yt-dlp.exe"),
                {"--skip-download", "--no-playlist", "--no-warnings", "--encoding", "utf-8",
                 "--print", "%(view_count)s|%(upload_date)s", "--print", "description",
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
    m_panel->setVisible(m_panelOpen && playing && m_pages->currentWidget() != m_videoPage && !m_videoFull);
    m_nowPlaying->setPanelOpen(m_panelOpen);
}

void MainWindow::refreshPanel()
{
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
    m_panel->setVideoState(m_mpv.property("user-data/ghost/audio").toBool(), songVideo(), m_findingVideo);
    m_panel->setPosition(m_mpv.property("time-pos").toDouble());

    // Credits and the artist, once per video.
    if (!id.isEmpty() && id != m_panelFacts) {
        m_panelFacts = id;
        m_panelChannel.clear();
        m_panel->setCredits({}, false);
        m_panel->setArtist(nullptr);
        fetchSongInfo(id, QStringLiteral("https://www.youtube.com/watch?v=") + id, [this, id](const QString &printed) {
            if (id != m_panelFacts)
                return;
            const songinfo::Facts facts = songinfo::parse(printed);
            m_panel->setCredits(facts.credits, true);
            const QString artist = playingSong().first;
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
        auto [nextTrack, nextArtist] = splitTitle(next.value("title").toString());
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

void MainWindow::setSongVideo(bool on)
{
    // The whole list turns to videos from this song on, until "Switch to
    // audio" or another list: ghost.lua plays each song as its video,
    // songmatch.lua finds it on YouTube. This song starts again at the same
    // second.
    if (!m_mpv.connected() || on == songVideo())
        return;
    if (on) {
        QString format = mpvconf::value("ytdl-format");
        if (format.isEmpty())
            format = QStringLiteral("bestvideo[height<=?1080]+bestaudio/best");
        m_mpv.setProperty("user-data/ghost/video-format", format);
        m_findingVideo = true;
        m_wantVideoPage = true;
    }
    m_mpv.setProperty("user-data/ghost/video-mode", on);
    m_mpv.setProperty("start", QString::number(m_mpv.property("time-pos").toDouble(), 'f', 1));
    m_resetStart = true;
    m_mpv.command({"playlist-play-index", m_mpv.property("playlist-pos").toInt()});
    refreshPanel();
}

void MainWindow::songVideoChanged()
{
    schedulePlace();
    m_findingVideo = false;
    m_videoPage->setSongVideo(songVideo());
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
        showFullScreen();
        m_videoPage->setFocus();
    } else if (m_wasMaximized) {
        showNormal();
        ShowWindow(reinterpret_cast<HWND>(winId()), SW_MAXIMIZE);
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
            if (all || pid != ours)
                TerminateProcess(h, 0);
            else
                keep << pid;
        }
        CloseHandle(h);
    }
    m_launchedPids = keep;
}

void MainWindow::rememberPlayed(const QString &path)
{
    QSettings settings;
    QStringList played = settings.value("recent/played").toStringList();
    played.removeAll(path);
    played.prepend(path);
    settings.setValue("recent/played", played.mid(0, 20));
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
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::addFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, "Add a folder with playlists");
    if (folder.isEmpty())
        return;
    m_library.addFolder(folder);
    rescan();
}
