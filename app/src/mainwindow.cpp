#include "mainwindow.h"

#include "theme.h"

#include <QButtonGroup>
#include <QCoreApplication>
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
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <functional>

namespace {

// The playlist's index in the library, kept on each card so filtering never
// loses track of which file a card is.
constexpr int kIndexRole = Qt::UserRole;

QString subtitleFor(const Playlist &p)
{
    return QStringLiteral("%1  ·  %2 %3")
        .arg(p.audio ? "Songs" : "Videos")
        .arg(p.count)
        .arg(p.count == 1 ? "item" : "items");
}

// A cover drawn to fill its rectangle with rounded corners, or a calm tinted
// block with the list's first letter while the real one is on its way.
void paintCover(QPainter &painter, const QRectF &rect, const QPixmap &pixmap,
                const QString &name, qreal radius)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(rect, radius, radius);
    painter.setClipPath(clip);

    if (!pixmap.isNull()) {
        const QSizeF scaled = QSizeF(pixmap.size()).scaled(rect.size(), Qt::KeepAspectRatioByExpanding);
        const QRectF target(rect.center() - QPointF(scaled.width() / 2, scaled.height() / 2), scaled);
        painter.drawPixmap(target, pixmap, pixmap.rect());
    } else {
        painter.fillRect(rect, theme::placeholderFor(name));
        QFont font = painter.font();
        font.setPixelSize(int(rect.height() * 0.45));
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(theme::text().darker(115));
        painter.drawText(rect, Qt::AlignCenter, name.left(1).toUpper());
    }
    painter.restore();
}

} // namespace

// The big cover on the left, osu!-style: whatever is selected, shown large.
class CoverView : public QWidget {
public:
    using QWidget::QWidget;

    void show(const QPixmap &pixmap, const QString &name)
    {
        m_pixmap = pixmap;
        m_name = name;
        update();
    }

    QSize sizeHint() const override { return {480, 270}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w * 9 / 16; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QRectF r = rect();
        if (r.height() > r.width() * 9 / 16)
            r.setHeight(r.width() * 9 / 16);
        paintCover(painter, r, m_pixmap, m_name, 14);
    }

private:
    QPixmap m_pixmap;
    QString m_name;
};

// One playlist as a card: cover on the left, name and count beside it. The
// selected card is lit and slides out to the left, as songs do in osu!.
class CardDelegate : public QStyledItemDelegate {
public:
    using CoverFor = std::function<QPixmap(const QModelIndex &)>;

    CardDelegate(CoverFor coverFor, QObject *parent)
        : QStyledItemDelegate(parent), m_coverFor(std::move(coverFor)) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return {360, 84};
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const int slide = selected ? 0 : 22;
        QRectF card = QRectF(option.rect).adjusted(6 + slide, 5, -8, -5);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(card, 12, 12);
        painter->fillPath(path, selected ? theme::cardSelected() : hover ? theme::cardHover() : theme::card());
        if (selected) {
            painter->setPen(QPen(theme::accent(), 2));
            painter->drawPath(path);
        }

        const QRectF cover(card.left() + 8, card.top() + 8, (card.height() - 16) * 16 / 9, card.height() - 16);
        const QString name = index.data(Qt::DisplayRole).toString();
        paintCover(*painter, cover, m_coverFor(index), name, 8);

        const QRectF text(cover.right() + 14, card.top() + 12, card.right() - cover.right() - 26, card.height() - 24);
        QFont titleFont = option.font;
        titleFont.setPointSizeF(option.font.pointSizeF() + 1.5);
        titleFont.setWeight(QFont::DemiBold);
        painter->setFont(titleFont);
        painter->setPen(theme::text());
        const QString title = QFontMetrics(titleFont).elidedText(name, Qt::ElideRight, int(text.width()));
        painter->drawText(text, Qt::AlignLeft | Qt::AlignTop, title);

        painter->setFont(option.font);
        painter->setPen(selected ? theme::accentText() : theme::muted());
        painter->drawText(text, Qt::AlignLeft | Qt::AlignBottom, index.data(Qt::ToolTipRole + 1).toString());
        painter->restore();
    }

private:
    CoverFor m_coverFor;
};

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("Ghost Engine"));
    resize(1100, 680);

    // -- top bar: categories, search, folders
    m_chips = new QButtonGroup(this);
    auto *top = new QHBoxLayout;
    top->setSpacing(8);
    int id = 0;
    for (const char *label : {"All", "Songs", "Videos"}) {
        auto *chip = new QPushButton(label);
        chip->setObjectName("chip");
        chip->setCheckable(true);
        chip->setChecked(id == 0);
        m_chips->addButton(chip, id++);
        top->addWidget(chip);
    }
    top->addSpacing(12);
    m_search = new QLineEdit;
    m_search->setPlaceholderText("Search playlists");
    m_search->setClearButtonEnabled(true);
    top->addWidget(m_search, 1);
    auto *addFolderButton = new QPushButton("Add folder");
    auto *refreshButton = new QPushButton("Refresh");
    top->addWidget(addFolderButton);
    top->addWidget(refreshButton);

    // -- left: the selected list, large
    m_hero = new CoverView;
    m_hero->setMinimumWidth(380);
    m_title = new QLabel;
    m_title->setObjectName("heroTitle");
    m_title->setWordWrap(true);
    m_subtitle = new QLabel;
    m_subtitle->setObjectName("heroSubtitle");
    auto *playButton = new QPushButton("Play");
    playButton->setObjectName("primary");
    auto *shuffleButton = new QPushButton("Shuffle");
    auto *heroButtons = new QHBoxLayout;
    heroButtons->addWidget(playButton, 1);
    heroButtons->addWidget(shuffleButton);
    auto *left = new QVBoxLayout;
    left->setSpacing(10);
    left->addWidget(m_hero);
    left->addSpacing(6);
    left->addWidget(m_title);
    left->addWidget(m_subtitle);
    left->addSpacing(6);
    left->addLayout(heroButtons);
    left->addStretch();

    // -- right: the cards
    m_cards = new QListWidget;
    m_cards->setObjectName("cards");
    m_cards->setMouseTracking(true);
    m_cards->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_cards->setContextMenuPolicy(Qt::CustomContextMenu);
    m_cards->setItemDelegate(new CardDelegate([this](const QModelIndex &index) {
        const auto &all = m_library.playlists();
        const int i = index.data(kIndexRole).toInt();
        return i >= 0 && i < all.size()
            ? m_covers.get(coverId(all[i]), ThumbnailCache::Size::Card) : QPixmap();
    }, m_cards));

    auto *body = new QHBoxLayout;
    body->setSpacing(18);
    body->addLayout(left, 5);
    body->addWidget(m_cards, 6);

    m_status = new QLabel;
    m_status->setObjectName("status");

    auto *root = new QVBoxLayout;
    root->setContentsMargins(20, 16, 20, 10);
    root->setSpacing(14);
    root->addLayout(top);
    root->addLayout(body, 1);
    root->addWidget(m_status);
    auto *central = new QWidget;
    central->setLayout(root);
    setCentralWidget(central);

    connect(m_chips, &QButtonGroup::idClicked, this, [this](int which) {
        m_category = static_cast<Category>(which);
        refill();
    });
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::refill);
    connect(m_cards, &QListWidget::currentRowChanged, this, &MainWindow::showSelection);
    connect(m_cards, &QListWidget::itemActivated, this, [this] { play(); });
    connect(m_cards, &QListWidget::customContextMenuRequested, this, &MainWindow::showContextMenu);
    connect(playButton, &QPushButton::clicked, this, [this] { play(); });
    connect(shuffleButton, &QPushButton::clicked, this, [this] { play(true); });
    connect(addFolderButton, &QPushButton::clicked, this, &MainWindow::addFolder);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::rescan);
    m_resolver.setYtDlp(QFileInfo(findMpv()).absoluteDir().filePath("yt-dlp.exe"));
    connect(&m_resolver, &CoverResolver::resolved, this, [this] {
        m_cards->viewport()->update();
        showSelection();
    });
    connect(&m_covers, &ThumbnailCache::ready, this, [this] {
        m_cards->viewport()->update();
        showSelection();
    });

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
    m_chips->button(0)->setText(QStringLiteral("All  %1").arg(all.size()));
    m_chips->button(1)->setText(QStringLiteral("Songs  %1").arg(songs));
    m_chips->button(2)->setText(QStringLiteral("Videos  %1").arg(all.size() - songs));

    const QString needle = m_search->text().trimmed();
    const QString keep = selectedPlaylist() ? selectedPlaylist()->path : QString();

    m_cards->clear();
    int restore = 0;
    for (int i = 0; i < all.size(); ++i) {
        const Playlist &p = all[i];
        if (m_category == Category::Songs && !p.audio)
            continue;
        if (m_category == Category::Videos && p.audio)
            continue;
        if (!needle.isEmpty() && !p.name.contains(needle, Qt::CaseInsensitive))
            continue;
        auto *card = new QListWidgetItem(p.name);
        card->setData(kIndexRole, i);
        card->setData(Qt::ToolTipRole + 1, subtitleFor(p));
        card->setToolTip(QDir::toNativeSeparators(p.path));
        if (p.path == keep)
            restore = m_cards->count();
        m_cards->addItem(card);
    }
    if (m_cards->count())
        m_cards->setCurrentRow(restore);
    showSelection();

    m_status->setText(findMpv().isEmpty()
        ? QStringLiteral("mpv.exe not found. Put Ghost Engine in your mpv folder.")
        : QStringLiteral("%1 playlists from %2 folder(s)").arg(all.size()).arg(m_library.folders().size()));
}

void MainWindow::showSelection()
{
    const Playlist *p = selectedPlaylist();
    if (!p) {
        m_hero->show({}, QString());
        m_title->setText(m_cards->count() ? QString() : QStringLiteral("No playlists here yet"));
        m_subtitle->setText(m_cards->count() ? QString()
            : QStringLiteral("Export one with the bot, or add the folder you keep them in."));
        return;
    }
    m_hero->show(m_covers.get(coverId(*p), ThumbnailCache::Size::Hero), p->name);
    m_title->setText(p->name);
    m_subtitle->setText(subtitleFor(*p) + QStringLiteral("  ·  ") +
                        QLocale().toString(p->modified, QLocale::ShortFormat));
}

QString MainWindow::coverId(const Playlist &p)
{
    // The first entry that has a video: its own id, or the one songmatch.lua
    // found for its search.
    QString firstSearch;
    for (const QString &target : p.preview) {
        QString id = youtube::videoId(target);
        const QString query = youtube::searchQuery(target);
        if (id.isEmpty() && !query.isEmpty()) {
            id = m_matches.idFor(query);
            if (firstSearch.isEmpty())
                firstSearch = query;
        }
        if (!id.isEmpty())
            return id;
    }
    // Nothing played yet: look the first song up, for its picture only.
    return m_resolver.idFor(firstSearch);
}

const Playlist *MainWindow::selectedPlaylist() const
{
    const QListWidgetItem *card = m_cards->currentItem();
    if (!card)
        return nullptr;
    const int index = card->data(kIndexRole).toInt();
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

void MainWindow::play(bool shuffle)
{
    const Playlist *p = selectedPlaylist();
    if (!p)
        return;
    const QString mpv = findMpv();
    if (mpv.isEmpty()) {
        QMessageBox::warning(this, "Ghost Engine",
                             "mpv.exe wasn't found. Install Ghost Engine with install.bat first.");
        return;
    }
    QStringList args;
    if (shuffle)
        args << "--shuffle";
    args << "--" << p->path;
    // Detached and from mpv's own folder, the way a double-click on the .m3u
    // would start it, so its scripts and the presence agent behave the same.
    QProcess::startDetached(mpv, args, QFileInfo(mpv).absolutePath());
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
    if (!m_cards->itemAt(pos))
        return;
    QMenu menu(this);
    menu.addAction("Play", this, [this] { play(); });
    menu.addAction("Shuffle", this, [this] { play(true); });
    menu.addSeparator();
    menu.addAction("Show in folder", this, &MainWindow::showSelectedInFolder);
    menu.exec(m_cards->viewport()->mapToGlobal(pos));
}
