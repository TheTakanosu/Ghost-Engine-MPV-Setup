#include "mainwindow.h"

#include "covercrop.h"
#include "icons.h"
#include "nowplaying.h"
#include "theme.h"
#include "tiles.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QCollator>
#include <QColorDialog>
#include <QCoreApplication>
#include <QCursor>
#include <QDropEvent>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QImage>
#include <QKeyEvent>
#include <QShortcut>
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
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
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
            shadow.addRoundedRect(QRectF(c).adjusted(-i, -i + 4, i, i + 4), 10 + i, 10 + i);
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
        if (name == "playlist-pos" || name == "playlist-path" || name == "path")
            markPlaying();
        if (name == "playlist-count" || name == "playlist-path" || name == "path")
            applyPendingOrder();
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
        const Playlist list = m_library.playlists().value(item->data(tiles::kIndexRole).toInt());
        const QString path = list.path;
        QMenu menu(this);
        menu.addAction("Play", this, [this, path] { play(path); });
        menu.addAction("Shuffle", this, [this, path] { play(path, true); });
        const int index = item->data(tiles::kIndexRole).toInt();
        const QString listKey = item->data(tiles::kKeyRole).toString();
        menu.addSeparator();
        menu.addAction(item->data(tiles::kPinnedRole).toBool() ? "Unpin playlist" : "Pin playlist", this,
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
            menu.addAction("Use the songs' covers again", this, [this, index] { removeCover(index); });
        menu.addAction("Export a copy...", this, [this, index] { exportPlaylist(index); });
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
    // The title stays in the layout, only invisible, so nothing around it moves.
    m_playlistName->setStyleSheet(m_playlistName->styleSheet() + "color: transparent;");
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
    if (watched == m_libraryPanel && event->type() == QEvent::Resize && m_rowDelegate) {
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
    if (m_songs && watched == m_songs->viewport() && event->type() == QEvent::MouseButtonRelease) {
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
        icons::paint(p, icons::Icon::Clock, QRectF(w->width() - 16, 1, 16, 16), ink);
        if (active) {
            p.setPen(theme::accent());
            p.drawText(QRectF(0, 0, w->width() - 22, w->height()), Qt::AlignRight | Qt::AlignVCenter,
                       m_sortDescending ? QStringLiteral("▾") : QStringLiteral("▴"));
        }
        return true;
    }
    if (watched == m_playlistCover && m_open >= 0) {
        auto *header = static_cast<PlaylistHeader *>(m_playlistCover);
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
    const bool canDrag = m_sortKey == SortKey::Custom && needle.isEmpty();
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
    m_pendingPath = path;
    m_pendingOrder = order;
    // A list that never loads should not reorder a later one.
    const int serial = ++m_pendingSerial;
    if (!order.isEmpty())
        QTimer::singleShot(15000, this, [this, serial] { if (m_pendingSerial == serial) m_pendingOrder.clear(); });
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
