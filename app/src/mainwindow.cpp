#include "mainwindow.h"

#include "icons.h"
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
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QStandardPaths>
#include <QVariantAnimation>
#include <QWheelEvent>
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
constexpr int kTopBar = 50;         // height of the draggable strip: 8 px under the search box
constexpr int kHeaderTall = 264;    // playlist header, scrolled to the top
constexpr int kHeaderShort = 92;    // ... and once the songs are scrolled
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

// The top of a playlist page: its collage, and behind it a wash of the
// covers' own colour fading into the page, the way Spotify tints a playlist.
class PlaylistHeader : public QWidget {
public:
    using Source = std::function<QVector<QPixmap>()>;
    explicit PlaylistHeader(Source source) : m_source(std::move(source)) {}
    void setName(const QString &name) { m_name = name; update(); }
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
        if (!covers.isEmpty() && !covers.first().isNull()) {
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
            shadow.addRoundedRect(QRectF(c).adjusted(-i, -i + 4, i, i + 4), 10 + i, 10 + i);
            p.fillPath(shadow, QColor(0, 0, 0, 14));
        }
        tiles::paintCollage(p, c, covers, m_name, 10);
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

    auto *mainPanel = new QWidget;
    mainPanel->setObjectName("panel");
    mainPanel->setAttribute(Qt::WA_StyledBackground);
    auto *mainBox = new QVBoxLayout(mainPanel);
    mainBox->setContentsMargins(0, 0, 0, 0);
    mainBox->addWidget(m_pages);

    m_split = new QSplitter(Qt::Horizontal);
    m_split->addWidget(buildLibrary());
    m_split->addWidget(mainPanel);
    m_split->setStretchFactor(1, 1);
    m_split->setHandleWidth(8);
    m_split->setChildrenCollapsible(false);
    m_split->setSizes({300, 940});
    connect(m_split, &QSplitter::splitterMoved, this, [this] {
        setLibraryCompact(m_split->sizes().value(0) < kCompactBelow);
        syncSearchWidth();
    });

    m_nowPlaying = new NowPlayingBar(&m_mpv, &m_covers);

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
    connect(&m_resolver, &CoverResolver::resolved, this, [this, repaint] {
        fillDurations();
        repaint();
    });
    connect(&m_covers, &ThumbnailCache::ready, this, repaint);
    connect(&m_mpv, &MpvClient::propertyChanged, this, [this, repaint](const QString &name) {
        // A song played since the last look may have given a list its cover.
        if (name == "user-data/ghost/source")
            QTimer::singleShot(500, repaint);
        if (name == "playlist-pos" || name == "playlist-path")
            markPlaying();
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
    m_songs->viewport()->installEventFilter(this);

    // Lists added, changed or deleted on disk show up without Refresh.
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_rescanSoon.start(); });
    m_rescanSoon.setSingleShot(true);
    m_rescanSoon.setInterval(600);
    connect(&m_rescanSoon, &QTimer::timeout, this, [this] {
        if (QSettings().value("library/autoImport").toBool())
            importStray(false);
        rescan();
    });

    rescan();
    showHome();
    QTimer::singleShot(800, this, [this] { importStray(true); });
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
    // Through Windows itself rather than Qt's showMaximized(): Qt resizes a
    // frameless window to the screen, so Windows took the full size for the
    // normal one, and dragging the window off maximised kept it full size.
    connect(m_maxButton, &QPushButton::clicked, this, [this] {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
    });
    connect(close, &QPushButton::clicked, this, &MainWindow::close);
    m_chromeButtons = {home, m_search, minimize, m_maxButton, close};

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
        const int index = item->data(tiles::kIndexRole).toInt();
        menu.addSeparator();
        menu.addAction("Rename", this, [this, index] { renamePlaylist(index); });
        menu.addAction("Show in folder", this, [path] {
            QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
        });
        menu.addSeparator();
        menu.addAction("Delete", this, [this, index] { deletePlaylist(index); });
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
    // A scroll bar in a cover-wide panel would cut the covers off.
    m_libraryList->setVerticalScrollBarPolicy(compact ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
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
    m_playlistName->installEventFilter(this);
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
    connect(playButton, &QAbstractButton::clicked, this, [this] {
        if (m_open >= 0) play(m_library.playlists()[m_open].path);
    });
    connect(shuffleButton, &QAbstractButton::clicked, this, [this] {
        if (m_open >= 0) play(m_library.playlists()[m_open].path, true);
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
    actions->addStretch();

    // "#  Title" over the songs, with a hairline under it.
    auto *columns = new QWidget;
    columns->setObjectName("columns");
    columns->setAttribute(Qt::WA_StyledBackground);
    auto *colRow = new QHBoxLayout(columns);
    colRow->setContentsMargins(0, 6, 0, 8);
    auto *hash = new QLabel("#");
    hash->setObjectName("columnLabel");
    hash->setFixedWidth(44);
    hash->setAlignment(Qt::AlignCenter);
    auto *titleLabel = new QLabel("Title");
    titleLabel->setObjectName("columnLabel");
    colRow->addWidget(hash);
    colRow->addSpacing(4);
    colRow->addWidget(titleLabel, 1);
    m_addedHeader = new QLabel("Date added");
    m_addedHeader->setObjectName("columnLabel");
    m_addedHeader->setFixedWidth(tiles::kAddedColumn);
    colRow->addWidget(m_addedHeader);
    auto *clock = new QWidget;
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
    m_songs->setItemDelegate(new tiles::SongDelegate([this](const QModelIndex &index) {
        return m_covers.get(idForTarget(index.data(Qt::UserRole).toString()), ThumbnailCache::Size::Card);
    }, m_songs));
    // Scrolling the songs folds the header into a slim bar, as Spotify does:
    // the cover and title shrink over the first 170 px of scrolling.
    connect(m_songs->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        const qreal t = qBound(0.0, value / 170.0, 1.0);
        m_playlistCover->setFixedHeight(int(kHeaderTall - t * (kHeaderTall - kHeaderShort)));
        layoutHeader();
    });
    connect(m_songs, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (m_open >= 0)
            play(m_library.playlists()[m_open].path, false, m_songs->row(item));
    });

    auto *list = new QVBoxLayout;
    list->setContentsMargins(20, 0, 16, 0);
    list->setSpacing(4);
    list->addWidget(columns);
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
    m_playlistName->setStyleSheet(QStringLiteral("font-size: %1pt;").arg(size));
    m_playlistName->setText(QFontMetrics(font).elidedText(name, Qt::ElideRight, qMax(40, available)));
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
    m_nameEdit->setStyleSheet(m_playlistName->styleSheet());
    m_nameEdit->setGeometry(m_playlistName->geometry().adjusted(-8, -4, 8, 4));
    m_playlistName->setVisible(false);
    m_nameEdit->show();
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void MainWindow::finishRename()
{
    if (!m_nameEdit->isVisible())
        return;
    m_nameEdit->hide();
    m_playlistName->setVisible(true);
    if (m_open >= 0)
        renameTo(m_open, m_nameEdit->text().trimmed());
}

void MainWindow::renameTo(int index, const QString &typed)
{
    const auto &all = m_library.playlists();
    if (index < 0 || index >= all.size())
        return;
    const Playlist p = all[index];
    if (typed.isEmpty() || typed == p.name)
        return;
    static const QRegularExpression forbidden(QStringLiteral(R"([\\/:*?"<>|])"));
    if (typed.contains(forbidden)) {
        QMessageBox::warning(this, "Rename playlist", "A name can't contain \\ / : * ? \" < > |");
        return;
    }
    const QFileInfo file(p.path);
    const QString target = file.absoluteDir().filePath(typed + '.' + file.suffix());
    if (QFileInfo::exists(target) || !QFile::rename(p.path, target)) {
        QMessageBox::warning(this, "Rename playlist", "There's already a playlist with that name here.");
        return;
    }
    movedPlayed(p.path, target);
    reopenAfter(target);
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
        const int seconds = m_resolver.durationFor(idForTarget(item->data(Qt::UserRole).toString()));
        item->setData(tiles::kDurationRole, seconds > 0
            ? QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0')) : QString());
    }
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
    // The play mark in a song's number column starts the list from there.
    if (watched == m_songs->viewport() && event->type() == QEvent::MouseButtonRelease) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QListWidgetItem *item = m_songs->itemAt(mouse->position().toPoint());
        if (item && mouse->button() == Qt::LeftButton && mouse->position().x() < 48 && m_open >= 0) {
            play(m_library.playlists()[m_open].path, false, m_songs->row(item));
            return true;
        }
    }
    if (watched->objectName() == "clockHeader" && event->type() == QEvent::Paint) {
        auto *w = static_cast<QWidget *>(watched);
        QPainter p(w);
        icons::paint(p, icons::Icon::Clock, QRectF(w->width() - 16, 1, 16, 16), theme::muted());
        return true;
    }
    if (watched == m_playlistName && event->type() == QEvent::MouseButtonDblClick) {
        renamePlaylist(m_open);
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    layoutHeader();
    syncSearchWidth();
    if (m_addedHeader)
        m_addedHeader->setVisible(m_songs->viewport()->width() > 700);
}

void MainWindow::syncSearchWidth()
{
    // The search box spans the library panel below it, edge to edge.
    if (!m_search || !m_split)
        return;
    const int panel = m_split->sizes().value(0);
    m_search->setFixedWidth(qMax(220, panel - 42));   // less the Home button and its gap
}

void MainWindow::markPlaying()
{
    // The song mpv is on, when the list open here is the one it plays.
    const auto &all = m_library.playlists();
    const bool same = m_open >= 0 && m_open < all.size() && m_mpv.connected() &&
        QFileInfo(m_mpv.property("playlist-path").toString()).absoluteFilePath()
            .compare(QFileInfo(all[m_open].path).absoluteFilePath(), Qt::CaseInsensitive) == 0;
    const int pos = same ? m_mpv.property("playlist-pos").toInt() : -1;
    for (int row = 0; row < m_songs->count(); ++row)
        m_songs->item(row)->setData(tiles::kPlayingRole, row == pos);
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
    m_playlistKind->setText(p.audio ? "PLAYLIST  ·  SONGS" : "PLAYLIST  ·  VIDEOS");
    m_playlistInfo->setText(subtitleFor(p) + QStringLiteral("  ·  added ") +
                            QLocale().toString(p.modified.date(), QLocale::ShortFormat));
    static_cast<PlaylistHeader *>(m_playlistCover)->setName(p.name);

    m_songs->clear();
    const QString added = relativeTime(p.modified);
    int number = 1;
    for (const PlaylistEntry &entry : p.readEntries()) {
        const auto [track, artist] = splitTitle(entry.title);
        auto *row = new QListWidgetItem(track);
        row->setData(Qt::UserRole, entry.target);
        row->setData(tiles::kNumberRole, number++);
        row->setData(tiles::kArtistRole, artist);
        row->setData(tiles::kAddedRole, added);
        m_songs->addItem(row);
    }
    markPlaying();
    fillDurations();
    m_pages->setCurrentIndex(1);
    m_songs->scrollToTop();
    m_playlistCover->setFixedHeight(kHeaderTall);
    layoutHeader();
    // Ask for every song's cover now rather than as each row scrolls into
    // view, so the list fills in while it is being looked at.
    for (int row = 0; row < m_songs->count(); ++row)
        m_covers.get(idForTarget(m_songs->item(row)->data(Qt::UserRole).toString()), ThumbnailCache::Size::Card);
}

// -- content -------------------------------------------------------------------

void MainWindow::rescan()
{
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    m_watcher.addPaths(m_library.folders());
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
