#include "mainwindow.h"

#include "nowplaying.h"
#include "theme.h"
#include "tiles.h"

#include <QCloseEvent>
#include <QCoreApplication>
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
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kTile = 168;          // cover size in the grids
constexpr int kRecentCount = 8;     // tiles in each Home row
constexpr int kMinutesPerSong = 3;  // for the "hours of music" estimate

QString subtitleFor(const Playlist &p)
{
    return QStringLiteral("%1 · %2 %3").arg(p.audio ? "Songs" : "Videos").arg(p.count)
                                       .arg(p.audio ? (p.count == 1 ? "song" : "songs")
                                                    : (p.count == 1 ? "video" : "videos"));
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

} // namespace

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("Ghost Engine"));
    resize(1240, 780);

    m_pages = new QStackedWidget;
    m_pages->addWidget(buildHome());
    m_pages->addWidget(buildBrowse());

    m_split = new QSplitter(Qt::Horizontal);
    m_split->addWidget(buildSidebar());
    m_split->addWidget(m_pages);
    m_split->setStretchFactor(1, 1);
    m_split->setHandleWidth(6);
    m_split->setChildrenCollapsible(false);
    m_split->setSizes({230, 1010});

    m_nowPlaying = new NowPlayingBar(&m_mpv, &m_covers);

    auto *root = new QVBoxLayout;
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_split, 1);
    root->addWidget(m_nowPlaying);
    auto *central = new QWidget;
    central->setLayout(root);
    setCentralWidget(central);

    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    m_split->restoreState(settings.value("window/split").toByteArray());

    m_resolver.setYtDlp(QFileInfo(findMpv()).absoluteDir().filePath("yt-dlp.exe"));
    auto repaint = [this] {
        for (QListWidget *v : {m_grid, m_recentAdded, m_recentPlayed})
            v->viewport()->update();
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
    showPage(Home);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/split", m_split->saveState());
    QMainWindow::closeEvent(event);
}

// -- layout ------------------------------------------------------------------

QWidget *MainWindow::buildSidebar()
{
    auto *brand = new QLabel("Ghost Engine");
    brand->setObjectName("brand");

    m_nav = new QListWidget;
    m_nav->setObjectName("nav");
    m_nav->addItems({"Home", "All playlists", "Songs", "Videos"});
    connect(m_nav, &QListWidget::currentRowChanged, this, &MainWindow::showPage);

    auto *addFolderButton = new QPushButton("Add folder");
    auto *refreshButton = new QPushButton("Refresh");
    connect(addFolderButton, &QPushButton::clicked, this, &MainWindow::addFolder);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::rescan);

    auto *side = new QWidget;
    side->setObjectName("sidebar");
    side->setAttribute(Qt::WA_StyledBackground);
    side->setMinimumWidth(170);
    auto *box = new QVBoxLayout(side);
    box->setContentsMargins(14, 18, 14, 14);
    box->setSpacing(10);
    box->addWidget(brand);
    box->addSpacing(6);
    box->addWidget(m_nav, 1);
    box->addWidget(addFolderButton);
    box->addWidget(refreshButton);
    return side;
}

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

    m_recentAdded = makeGrid(false);
    m_recentPlayed = makeGrid(false);
    m_recentPlayedTitle = sectionTitle("Jump back in");

    auto *page = new QWidget;
    auto *box = new QVBoxLayout(page);
    box->setContentsMargins(28, 24, 28, 16);
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

QWidget *MainWindow::buildBrowse()
{
    m_browseTitle = new QLabel;
    m_browseTitle->setObjectName("pageTitle");
    m_search = new QLineEdit;
    m_search->setPlaceholderText("Search playlists");
    m_search->setClearButtonEnabled(true);
    m_search->setMaximumWidth(360);
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::refill);

    auto *head = new QHBoxLayout;
    head->addWidget(m_browseTitle);
    head->addStretch();
    head->addWidget(m_search, 1);

    m_grid = makeGrid(true);

    auto *page = new QWidget;
    auto *box = new QVBoxLayout(page);
    box->setContentsMargins(28, 24, 20, 8);
    box->setSpacing(14);
    box->addLayout(head);
    box->addWidget(m_grid, 1);
    return page;
}

QListWidget *MainWindow::makeGrid(bool wrap)
{
    auto *grid = new QListWidget;
    grid->setObjectName("grid");
    grid->setViewMode(QListView::IconMode);
    grid->setMovement(QListView::Static);
    grid->setResizeMode(QListView::Adjust);
    grid->setWrapping(wrap);
    grid->setFlow(QListView::LeftToRight);
    grid->setUniformItemSizes(true);
    grid->setMouseTracking(true);
    grid->setSpacing(4);
    grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    grid->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    grid->setContextMenuPolicy(Qt::CustomContextMenu);
    grid->setItemDelegate(new tiles::TileDelegate(kTile, [this](const QModelIndex &index) {
        return coversFor(index.data(tiles::kIndexRole).toInt());
    }, grid));
    if (!wrap) {
        grid->setFixedHeight(kTile + 70 + 26);
        grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        grid->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    connect(grid, &QListWidget::itemActivated, this, [this, grid] {
        if (const Playlist *p = playlistAt(grid))
            play(p->path);
    });
    connect(grid, &QListWidget::customContextMenuRequested, this,
            [this, grid](const QPoint &pos) { showContextMenu(grid, pos); });
    return grid;
}

// -- content -----------------------------------------------------------------

void MainWindow::rescan()
{
    m_library.rescan();
    refill();
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
    m_nav->item(AllLists)->setText(QStringLiteral("All playlists   %1").arg(all.size()));
    m_nav->item(Songs)->setText(QStringLiteral("Songs   %1").arg(songLists));
    m_nav->item(Videos)->setText(QStringLiteral("Videos   %1").arg(all.size() - songLists));

    // Home
    m_greeting->setText(greetingFor(QTime::currentTime()));
    m_statLists->setText(QString::number(all.size()));
    m_statSongs->setText(QString::number(items));
    m_statHours->setText(QString::number((items * kMinutesPerSong + 30) / 60));
    m_statNew->setText(QString::number(fresh));

    QVector<int> newest;
    for (int i = 0; i < all.size() && newest.size() < kRecentCount; ++i)
        newest << i;   // the library is already newest first
    fillGrid(m_recentAdded, newest);

    QVector<int> played;
    for (const QString &path : QSettings().value("recent/played").toStringList()) {
        for (int i = 0; i < all.size(); ++i)
            if (all[i].path.compare(path, Qt::CaseInsensitive) == 0) { played << i; break; }
        if (played.size() >= kRecentCount)
            break;
    }
    fillGrid(m_recentPlayed, played);
    m_recentPlayedTitle->setVisible(!played.isEmpty());
    m_recentPlayed->setVisible(!played.isEmpty());

    // Browse
    const int page = m_nav->currentRow();
    const QString needle = m_search->text().trimmed();
    QVector<int> shown;
    for (int i = 0; i < all.size(); ++i) {
        const Playlist &p = all[i];
        if ((page == Songs && !p.audio) || (page == Videos && p.audio))
            continue;
        if (!needle.isEmpty() && !p.name.contains(needle, Qt::CaseInsensitive))
            continue;
        shown << i;
    }
    fillGrid(m_grid, shown);

    if (!m_selectPath.isEmpty()) {
        for (int row = 0; row < m_grid->count(); ++row) {
            const int i = m_grid->item(row)->data(tiles::kIndexRole).toInt();
            if (all[i].path.compare(m_selectPath, Qt::CaseInsensitive) == 0) {
                m_grid->setCurrentRow(row);
                m_grid->scrollToItem(m_grid->item(row));
            }
        }
        m_selectPath.clear();
    }
}

void MainWindow::fillGrid(QListWidget *grid, const QVector<int> &indices)
{
    const auto &all = m_library.playlists();
    grid->clear();
    for (int i : indices) {
        auto *tile = new QListWidgetItem(all[i].name);
        tile->setData(tiles::kIndexRole, i);
        tile->setData(tiles::kSubtitleRole, subtitleFor(all[i]));
        tile->setToolTip(QDir::toNativeSeparators(all[i].path));
        grid->addItem(tile);
    }
}

void MainWindow::showPage(int page)
{
    if (m_nav->currentRow() != page) {
        m_nav->setCurrentRow(page);   // re-enters through the signal
        return;
    }
    m_pages->setCurrentIndex(page == Home ? 0 : 1);
    static const char *titles[] = {"", "All playlists", "Songs", "Videos"};
    m_browseTitle->setText(titles[page]);
    refill();
}

// -- covers ------------------------------------------------------------------

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
    // Each entry's own video, or the one songmatch.lua matched for its search,
    // or - for songs nobody has played yet - a lookup made for the picture.
    QStringList ids;
    for (const QString &target : p.preview) {
        QString id = youtube::videoId(target);
        if (id.isEmpty()) {
            const QString query = youtube::searchQuery(target);
            id = m_matches.idFor(query);
            if (id.isEmpty())
                id = m_resolver.idFor(query);
        }
        if (!id.isEmpty() && !ids.contains(id))
            ids << id;
        if (ids.size() >= wanted)
            break;
    }
    return ids;
}

// -- actions -----------------------------------------------------------------

const Playlist *MainWindow::playlistAt(QListWidget *view) const
{
    const QListWidgetItem *tile = view->currentItem();
    if (!tile)
        return nullptr;
    const int index = tile->data(tiles::kIndexRole).toInt();
    const auto &all = m_library.playlists();
    return index >= 0 && index < all.size() ? &all[index] : nullptr;
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

void MainWindow::play(const QString &path, bool shuffle)
{
    rememberPlayed(path);
    // An mpv already open takes the list over IPC, so a second window does
    // not start on top of the first.
    if (m_mpv.connected()) {
        m_mpv.command({"loadfile", path, "replace"});
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
    args << "--" << path;
    // From mpv's own folder, the way a double-click on the .m3u starts it, so
    // its scripts and the presence agent behave the same.
    QProcess::startDetached(mpv, args, QFileInfo(mpv).absolutePath());
}

void MainWindow::playSelected(bool shuffle)
{
    QListWidget *view = qobject_cast<QListWidget *>(focusWidget());
    if (!view)
        view = m_grid;
    if (const Playlist *p = playlistAt(view))
        play(p->path, shuffle);
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
    m_selectPath = last;
    showPage(AllLists);
    refill();
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

void MainWindow::showContextMenu(QListWidget *view, const QPoint &pos)
{
    QListWidgetItem *tile = view->itemAt(pos);
    if (!tile)
        return;
    view->setCurrentItem(tile);
    const Playlist *p = playlistAt(view);
    if (!p)
        return;
    const QString path = p->path;
    QMenu menu(this);
    menu.addAction("Play", this, [this, path] { play(path); });
    menu.addAction("Shuffle", this, [this, path] { play(path, true); });
    menu.addSeparator();
    menu.addAction("Show in folder", this, [path] {
        QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
    });
    menu.exec(view->viewport()->mapToGlobal(pos));
}
