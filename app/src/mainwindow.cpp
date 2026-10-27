#include "mainwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// The playlist's index in the library, kept on each row so sorting and
// filtering never lose track of which file a row is.
constexpr int kIndexRole = Qt::UserRole;

QString countLabel(const QString &name, int count)
{
    return QStringLiteral("%1  (%2)").arg(name).arg(count);
}

} // namespace

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("Ghost Engine"));
    resize(960, 600);

    m_categories = new QListWidget;
    m_categories->setObjectName("categories");
    m_categories->setFixedWidth(180);
    m_categories->addItems({"All", "Songs", "Videos"});
    m_categories->setCurrentRow(0);

    auto *addFolderButton = new QPushButton("Add folder...");
    auto *refreshButton = new QPushButton("Refresh");
    auto *side = new QVBoxLayout;
    side->addWidget(m_categories, 1);
    side->addWidget(addFolderButton);
    side->addWidget(refreshButton);

    m_search = new QLineEdit;
    m_search->setPlaceholderText("Search playlists");
    m_search->setClearButtonEnabled(true);

    m_lists = new QTreeWidget;
    m_lists->setHeaderLabels({"Name", "Type", "Items", "Modified", "Folder"});
    m_lists->setRootIsDecorated(false);
    m_lists->setUniformRowHeights(true);
    m_lists->setSortingEnabled(true);
    m_lists->setContextMenuPolicy(Qt::CustomContextMenu);
    m_lists->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_lists->header()->setStretchLastSection(false);
    // Newest first, like the library itself: the list just exported is on top.
    m_lists->sortByColumn(3, Qt::DescendingOrder);

    auto *playButton = new QPushButton("Play");
    playButton->setObjectName("primary");
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(playButton);

    auto *main = new QVBoxLayout;
    main->addWidget(m_search);
    main->addWidget(m_lists, 1);
    main->addLayout(buttons);

    auto *root = new QHBoxLayout;
    root->addLayout(side);
    root->addLayout(main, 1);
    auto *central = new QWidget;
    central->setLayout(root);
    setCentralWidget(central);

    m_status = new QLabel;
    statusBar()->addWidget(m_status, 1);

    connect(m_categories, &QListWidget::currentRowChanged, this, &MainWindow::refill);
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::refill);
    connect(m_lists, &QTreeWidget::itemActivated, this, &MainWindow::playSelected);
    connect(m_lists, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(playButton, &QPushButton::clicked, this, &MainWindow::playSelected);
    connect(addFolderButton, &QPushButton::clicked, this, &MainWindow::addFolder);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::rescan);

    rescan();
}

void MainWindow::rescan()
{
    m_library.rescan();
    refill();
}

void MainWindow::refill()
{
    const auto &all = m_library.playlists();
    int songs = 0;
    for (const Playlist &p : all)
        songs += p.audio ? 1 : 0;
    m_categories->item(0)->setText(countLabel("All", all.size()));
    m_categories->item(1)->setText(countLabel("Songs", songs));
    m_categories->item(2)->setText(countLabel("Videos", all.size() - songs));

    const Category category = currentCategory();
    const QString needle = m_search->text().trimmed();

    m_lists->setSortingEnabled(false);
    m_lists->clear();
    for (int i = 0; i < all.size(); ++i) {
        const Playlist &p = all[i];
        if (category == Category::Songs && !p.audio)
            continue;
        if (category == Category::Videos && p.audio)
            continue;
        if (!needle.isEmpty() && !p.name.contains(needle, Qt::CaseInsensitive))
            continue;

        auto *row = new QTreeWidgetItem;
        row->setText(0, p.name);
        row->setText(1, p.audio ? "Songs" : "Videos");
        row->setData(2, Qt::DisplayRole, p.count);
        row->setData(3, Qt::DisplayRole, p.modified);
        row->setText(4, QDir::toNativeSeparators(QFileInfo(p.path).absolutePath()));
        row->setToolTip(0, QDir::toNativeSeparators(p.path));
        row->setData(0, kIndexRole, i);
        m_lists->addTopLevelItem(row);
    }
    m_lists->setSortingEnabled(true);
    for (int c = 1; c < m_lists->columnCount(); ++c)
        m_lists->resizeColumnToContents(c);

    const QString mpv = findMpv();
    m_status->setText(mpv.isEmpty()
        ? QStringLiteral("mpv.exe not found - put Ghost Engine next to it")
        : QStringLiteral("%1 playlists in %2 folder(s)   ·   mpv: %3")
              .arg(all.size()).arg(m_library.folders().size())
              .arg(QDir::toNativeSeparators(mpv)));
}

MainWindow::Category MainWindow::currentCategory() const
{
    switch (m_categories->currentRow()) {
    case 1: return Category::Songs;
    case 2: return Category::Videos;
    default: return Category::All;
    }
}

const Playlist *MainWindow::selectedPlaylist() const
{
    const QTreeWidgetItem *row = m_lists->currentItem();
    if (!row)
        return nullptr;
    const int index = row->data(0, kIndexRole).toInt();
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

void MainWindow::playSelected()
{
    const Playlist *p = selectedPlaylist();
    if (!p)
        return;
    const QString mpv = findMpv();
    if (mpv.isEmpty()) {
        QMessageBox::warning(this, "Ghost Engine",
                             "mpv.exe was not found. Install Ghost Engine with install.bat first.");
        return;
    }
    // Detached and from mpv's own folder, the way a double-click on the .m3u
    // would start it, so its scripts and the presence agent behave the same.
    QProcess::startDetached(mpv, {"--", p->path}, QFileInfo(mpv).absolutePath());
}

void MainWindow::addFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, "Add a folder with playlists");
    if (folder.isEmpty())
        return;
    m_library.addFolder(folder);
    rescan();
}

void MainWindow::showSelectedInFolder()
{
    const Playlist *p = selectedPlaylist();
    if (!p)
        return;
    QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(p->path)});
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    if (!m_lists->itemAt(pos))
        return;
    QMenu menu(this);
    menu.addAction("Play", this, &MainWindow::playSelected);
    menu.addAction("Show in folder", this, &MainWindow::showSelectedInFolder);
    menu.exec(m_lists->viewport()->mapToGlobal(pos));
}
