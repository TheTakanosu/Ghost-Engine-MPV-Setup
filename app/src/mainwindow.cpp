#include "mainwindow.h"

#include "nowplaying.h"
#include "theme.h"
#include "tiles.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>

#include <windows.h>
#include <dwmapi.h>

namespace {

constexpr int kTile = 168;          // cover size on the Home shelves
constexpr int kShelfCount = 8;      // tiles on each Home shelf
constexpr int kMinutesPerSong = 3;  // for the "hours of music" estimate
constexpr int kTopBar = 44;         // height of the draggable strip
constexpr int kCompactWidth = 84;   // the library panel, covers only
constexpr int kCompactBelow = 150;  // narrower than this, the panel goes compact

// Segoe Fluent Icons (Windows 11) / MDL2 Assets (10): same code points.
const QChar kIconHome(0xE80F), kIconMin(0xE921), kIconMax(0xE922), kIconRestore(0xE923),
    kIconClose(0xE8BB), kIconCollapse(0xE76B), kIconExpand(0xE76C), kIconAdd(0xE710);

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

// The playlist page's big cover: the same 2x2 collage as everywhere else.
class CollageView : public QWidget {
public:
    using Source = std::function<QVector<QPixmap>()>;
    CollageView(Source source) : m_source(std::move(source)) { setFixedSize(200, 200); }
    void setName(const QString &name) { m_name = name; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        tiles::paintCollage(p, rect(), m_source(), m_name, 12);
    }

private:
    Source m_source;
    QString m_name;
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

    m_split = new QSplitter(Qt::Horizontal);
    m_split->addWidget(buildLibrary());
    m_split->addWidget(m_pages);
    m_split->setStretchFactor(1, 1);
    m_split->setHandleWidth(6);
    m_split->setChildrenCollapsible(false);
    m_split->setSizes({300, 940});
    connect(m_split, &QSplitter::splitterMoved, this, [this] {
        setLibraryCompact(m_split->sizes().value(0) < kCompactBelow);
    });

    m_nowPlaying = new NowPlayingBar(&m_mpv, &m_covers);

    auto *root = new QVBoxLayout;
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildTopBar());
    root->addWidget(m_split, 1);
    root->addWidget(m_nowPlaying);
    auto *central = new QWidget;
    central->setObjectName("central");
    central->setLayout(root);
    setCentralWidget(central);

    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    m_split->restoreState(settings.value("window/split").toByteArray());
    setLibraryCompact(m_split->sizes().value(0) < kCompactBelow);

    m_resolver.setYtDlp(QFileInfo(findMpv()).absoluteDir().filePath("yt-dlp.exe"));
    auto repaint = [this] {
        for (QListWidget *v : {m_libraryList, m_recentAdded, m_recentPlayed, m_songs})
            v->viewport()->update();
        m_playlistCover->update();
        m_nowPlaying->refreshCover();
    };
    connect(&m_resolver, &CoverResolver::resolved, this, repaint);
    connect(&m_covers, &ThumbnailCache::ready, this, repaint);
    // A song played since the last look may have given a list its cover.
    connect(&m_mpv, &MpvClient::propertyChanged, this, [repaint](const QString &name) {
        if (name == "user-data/ghost/source")
            QTimer::singleShot(500, repaint);
    });

    rescan();
    showHome();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/split", m_split->saveState());
    QMainWindow::closeEvent(event);
}

// -- window chrome -----------------------------------------------------------

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
        // The whole window is client area. Maximised, Windows still puts the
        // invisible resize border outside the screen; take it back in.
        if (IsZoomed(msg->hwnd)) {
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
    connect(minimize, &QPushButton::clicked, this, &MainWindow::showMinimized);
    connect(m_maxButton, &QPushButton::clicked, this, [this] { isMaximized() ? showNormal() : showMaximized(); });
    connect(close, &QPushButton::clicked, this, &MainWindow::close);
    m_chromeButtons = {home, m_search, minimize, m_maxButton, close};

    m_topBar = new QWidget;
    m_topBar->setObjectName("topBar");
    m_topBar->setAttribute(Qt::WA_StyledBackground);
    m_topBar->setFixedHeight(kTopBar);
    auto *row = new QHBoxLayout(m_topBar);
    row->setContentsMargins(12, 0, 0, 0);
    row->setSpacing(8);
    row->addWidget(home);
    row->addWidget(m_search);
    row->addStretch();
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
    auto *add = glyphButton(kIconAdd, "Add a folder or refresh");
    connect(add, &QPushButton::clicked, this, [this, add] { showLibraryMenu(add->mapToGlobal(QPoint(0, add->height()))); });

    m_libraryHead = new QWidget;
    auto *head = new QHBoxLayout(m_libraryHead);
    head->setContentsMargins(0, 0, 0, 0);
    head->addWidget(m_collapse);
    head->addWidget(m_libraryTitle, 1);
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

    m_libraryList = new QListWidget;
    m_libraryList->setObjectName("library");
    m_libraryList->setMouseTracking(true);
    m_libraryList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_libraryList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_rowDelegate = new tiles::RowDelegate([this](const QModelIndex &index) {
        return coversFor(index.data(tiles::kIndexRole).toInt());
    }, m_libraryList);
    m_libraryList->setItemDelegate(m_rowDelegate);
    connect(m_libraryList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        openPlaylist(item->data(tiles::kIndexRole).toInt());
    });
    connect(m_libraryList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const auto &all = m_library.playlists();
        const int i = item->data(tiles::kIndexRole).toInt();
        if (i >= 0 && i < all.size())
            play(all[i].path);
    });
    connect(m_libraryList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_libraryList->itemAt(pos);
        if (!item)
            return;
        const QString path = m_library.playlists().value(item->data(tiles::kIndexRole).toInt()).path;
        QMenu menu(this);
        menu.addAction("Play", this, [this, path] { play(path); });
        menu.addAction("Shuffle", this, [this, path] { play(path, true); });
        menu.addSeparator();
        menu.addAction("Show in folder", this, [path] {
            QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
        });
        menu.exec(m_libraryList->viewport()->mapToGlobal(pos));
    });

    auto *panel = new QWidget;
    panel->setObjectName("libraryPanel");
    panel->setAttribute(Qt::WA_StyledBackground);
    panel->setMinimumWidth(kCompactWidth);
    auto *box = new QVBoxLayout(panel);
    box->setContentsMargins(10, 14, 6, 10);
    box->setSpacing(10);
    box->addWidget(m_libraryHead);
    box->addWidget(m_chipRow);
    box->addWidget(m_libraryList, 1);
    return panel;
}

void MainWindow::setLibraryCompact(bool compact)
{
    m_compact = compact;
    m_libraryTitle->setVisible(!compact);
    m_chipRow->setVisible(!compact);
    m_libraryHead->layout()->itemAt(2)->widget()->setVisible(!compact);
    m_collapse->setText(QString(compact ? kIconExpand : kIconCollapse));
    m_collapse->setToolTip(compact ? "Expand library" : "Collapse library");
    m_rowDelegate->setCompact(compact);
    m_libraryList->doItemsLayout();
}

void MainWindow::showLibraryMenu(const QPoint &at)
{
    QMenu menu(this);
    menu.addAction("Add a folder with playlists...", this, &MainWindow::addFolder);
    menu.addAction("Refresh", this, &MainWindow::rescan);
    menu.addSeparator();
    menu.addAction("Open the library folder", this, [] {
        QProcess::startDetached("explorer.exe", {QDir::toNativeSeparators(Library::homeFolder())});
    });
    menu.exec(at);
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
    page->setObjectName("page");
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
    auto *cover = new CollageView([this] { return coversFor(m_open); });
    m_playlistCover = cover;
    m_playlistKind = new QLabel;
    m_playlistKind->setObjectName("kicker");
    m_playlistName = new QLabel;
    m_playlistName->setObjectName("bigTitle");
    m_playlistName->setWordWrap(true);
    m_playlistInfo = new QLabel;
    m_playlistInfo->setObjectName("statLabel");

    auto *playButton = new QPushButton("Play");
    playButton->setObjectName("primary");
    playButton->setCursor(Qt::PointingHandCursor);
    auto *shuffleButton = new QPushButton("Shuffle");
    shuffleButton->setCursor(Qt::PointingHandCursor);
    connect(playButton, &QPushButton::clicked, this, [this] {
        if (m_open >= 0) play(m_library.playlists()[m_open].path);
    });
    connect(shuffleButton, &QPushButton::clicked, this, [this] {
        if (m_open >= 0) play(m_library.playlists()[m_open].path, true);
    });
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addWidget(playButton);
    buttons->addWidget(shuffleButton);
    buttons->addStretch();

    auto *text = new QVBoxLayout;
    text->setSpacing(6);
    text->addStretch();
    text->addWidget(m_playlistKind);
    text->addWidget(m_playlistName);
    text->addWidget(m_playlistInfo);
    text->addSpacing(8);
    text->addLayout(buttons);

    auto *header = new QHBoxLayout;
    header->setSpacing(24);
    header->addWidget(cover);
    header->addLayout(text, 1);

    m_songs = new QListWidget;
    m_songs->setObjectName("songs");
    m_songs->setMouseTracking(true);
    m_songs->setUniformItemSizes(true);
    m_songs->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_songs->setItemDelegate(new tiles::SongDelegate([this](const QModelIndex &index) {
        return m_covers.get(idForTarget(index.data(Qt::UserRole).toString()), ThumbnailCache::Size::Card);
    }, m_songs));
    connect(m_songs, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (m_open >= 0)
            play(m_library.playlists()[m_open].path, false, m_songs->row(item));
    });

    auto *page = new QWidget;
    page->setObjectName("page");
    auto *box = new QVBoxLayout(page);
    box->setContentsMargins(28, 22, 20, 8);
    box->setSpacing(18);
    box->addLayout(header);
    box->addWidget(m_songs, 1);
    return page;
}

void MainWindow::showHome()
{
    m_open = -1;
    m_libraryList->clearSelection();
    m_pages->setCurrentIndex(0);
}

void MainWindow::openPlaylist(int index)
{
    const auto &all = m_library.playlists();
    if (index < 0 || index >= all.size())
        return;
    m_open = index;
    const Playlist &p = all[index];
    m_playlistKind->setText(p.audio ? "SONG LIST" : "VIDEO LIST");
    m_playlistName->setText(p.name);
    m_playlistInfo->setText(subtitleFor(p) + QStringLiteral("  ·  added ") +
                            QLocale().toString(p.modified.date(), QLocale::ShortFormat));
    static_cast<CollageView *>(m_playlistCover)->setName(p.name);

    m_songs->clear();
    int number = 1;
    for (const PlaylistEntry &entry : p.readEntries()) {
        const auto [track, artist] = splitTitle(entry.title);
        auto *row = new QListWidgetItem(track);
        row->setData(Qt::UserRole, entry.target);
        row->setData(tiles::kNumberRole, number++);
        row->setData(tiles::kArtistRole, artist);
        m_songs->addItem(row);
    }
    m_pages->setCurrentIndex(1);
}

// -- content -------------------------------------------------------------------

void MainWindow::rescan()
{
    const QString openPath = m_open >= 0 ? m_library.playlists().value(m_open).path : QString();
    m_library.rescan();
    m_open = -1;
    refill();
    const auto &all = m_library.playlists();
    for (int i = 0; i < all.size(); ++i)
        if (all[i].path == openPath)
            openPlaylist(i);
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
    for (int i = 0; i < all.size(); ++i) {
        const Playlist &p = all[i];
        if ((m_filter == Songs && !p.audio) || (m_filter == Videos && p.audio))
            continue;
        if (!needle.isEmpty() && !p.name.contains(needle, Qt::CaseInsensitive))
            continue;
        auto *row = new QListWidgetItem(p.name);
        row->setData(tiles::kIndexRole, i);
        row->setData(tiles::kSubtitleRole, subtitleFor(p));
        row->setToolTip(p.name);
        m_libraryList->addItem(row);
        if (i == m_open)
            m_libraryList->setCurrentItem(row);
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

QVector<QPixmap> MainWindow::coversFor(int index)
{
    const auto &all = m_library.playlists();
    QVector<QPixmap> covers;
    if (index < 0 || index >= all.size())
        return covers;
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

void MainWindow::play(const QString &path, bool shuffle, int startAt)
{
    rememberPlayed(path);
    // An mpv already open takes the list over IPC, so a second window does
    // not start on top of the first.
    if (m_mpv.connected()) {
        m_mpv.command({"loadfile", path, "replace", -1, QStringLiteral("playlist-start=%1").arg(startAt)});
        if (shuffle)
            QTimer::singleShot(400, this, [this] { m_mpv.command({"playlist-shuffle"}); });
        m_mpv.setProperty("pause", false);
        return;
    }
    const QString mpv = findMpv();
    if (mpv.isEmpty()) {
        QMessageBox::warning(this, "Ghost Engine",
                             "mpv.exe wasn't found. Install Ghost Engine with install.bat first.");
        return;
    }
    QStringList args{QStringLiteral("--input-ipc-server=%1").arg(MpvClient::kPipe)};
    if (shuffle)
        args << "--shuffle";
    if (startAt > 0)
        args << QStringLiteral("--playlist-start=%1").arg(startAt);
    args << "--" << path;
    // From mpv's own folder, the way a double-click on the .m3u starts it, so
    // its scripts and the presence agent behave the same.
    QProcess::startDetached(mpv, args, QFileInfo(mpv).absolutePath());
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
