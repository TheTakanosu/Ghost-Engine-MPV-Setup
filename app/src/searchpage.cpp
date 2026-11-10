#include "searchpage.h"

#include "channelheader.h"

#include "icons.h"
#include "profile.h"
#include "theme.h"
#include "thumbnails.h"
#include "tiles.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <QSet>
#include <QSettings>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int kSongsShown = 5;
constexpr int kVideosShown = 4;
constexpr int kPlaylistsShown = 6;

QLabel *heading(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("sectionTitle");
    return label;
}

// A section: its heading with an optional "Show all" on the right, then
// whatever goes under it.
QWidget *section(const QString &title, QWidget *body, QPushButton *more = nullptr)
{
    auto *box = new QWidget;
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(8);
    auto *head = new QHBoxLayout;
    head->addWidget(heading(title));
    head->addStretch();
    if (more)
        head->addWidget(more);
    column->addLayout(head);
    column->addWidget(body);
    return box;
}

QPushButton *linkButton(const QString &text)
{
    auto *b = new QPushButton(text);
    b->setObjectName("link");
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// The cover a row asks for: a video's own thumbnail by its id, or the
// picture YouTube Music gave for a playlist.
QString coverKey(const ytmusic::Item &item)
{
    return item.kind == ytmusic::Kind::Playlist ? item.thumbnail : item.id;
}

} // namespace

SearchPage::SearchPage(ThumbnailCache &covers, QWidget *parent) : QWidget(parent), m_covers(covers)
{
    m_views = new QStackedWidget;
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_views);

    // -- Results --------------------------------------------------------------
    auto *content = new QWidget;
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(28, 22, 28, 28);
    column->setSpacing(26);

    // Music | YouTube: what the box searches. Remembered between runs.
    auto *modes = new QHBoxLayout;
    modes->setSpacing(8);
    m_musicMode = new QPushButton("Music");
    m_youtubeMode = new QPushButton("YouTube");
    for (QPushButton *b : {m_musicMode, m_youtubeMode}) {
        b->setObjectName("chip");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        modes->addWidget(b);
    }
    m_musicMode->setToolTip("Songs, music videos and playlists from YouTube Music");
    m_youtubeMode->setToolTip("Every video on YouTube, played here instead of in a browser");
    modes->addStretch();
    column->addLayout(modes);
    // The layout decides what the box searches now (the video button in the
    // top bar); the switch stays for the code, out of sight.
    m_musicMode->hide();
    m_youtubeMode->hide();
    m_youtube = QSettings().value("search/youtube", false).toBool();
    m_musicMode->setChecked(!m_youtube);
    m_youtubeMode->setChecked(m_youtube);
    connect(m_musicMode, &QPushButton::clicked, this, [this] { setYouTubeMode(false); });
    connect(m_youtubeMode, &QPushButton::clicked, this, [this] { setYouTubeMode(true); });

    m_status = new QLabel;
    m_status->setObjectName("heroInfo");
    m_status->setWordWrap(true);
    connect(m_status, &QLabel::linkActivated, this, [this] {
        const QString query = m_query;
        m_query.clear();
        setQuery(query);
    });
    column->addWidget(m_status);

    m_libraryChips = new QWidget;
    auto *chips = new QHBoxLayout(m_libraryChips);
    chips->setContentsMargins(0, 0, 0, 0);
    chips->setSpacing(8);
    m_librarySection = section("In your library", m_libraryChips);
    column->addWidget(m_librarySection);

    m_songsList = makeList(false);
    m_songsMore = linkButton("Show all");
    m_songsSection = section("Songs", m_songsList, m_songsMore);
    column->addWidget(m_songsSection);
    connect(m_songsMore, &QPushButton::clicked, this, [this] {
        const bool all = m_songsList->count() <= kSongsShown;
        fill(m_songsList, m_songs, all ? m_songs.size() : kSongsShown);
        m_songsMore->setText(all ? "Show less" : "Show all");
    });

    m_videosList = makeList(false);
    m_videosMore = linkButton("Show all");
    m_videosSection = section("Videos", m_videosList, m_videosMore);
    column->addWidget(m_videosSection);
    connect(m_videosMore, &QPushButton::clicked, this, [this] {
        const bool all = m_videosList->count() <= kVideosShown;
        fill(m_videosList, m_videos, all ? m_videos.size() : kVideosShown);
        m_videosMore->setText(all ? "Show less" : "Show all");
    });

    m_playlistsList = makeList(false);
    m_playlistsSection = section("Playlists", m_playlistsList);
    column->addWidget(m_playlistsSection);

    m_youtubeList = makeList(false);
    m_youtubeMore = linkButton("Show more");
    auto *moreRow = new QHBoxLayout;
    moreRow->addStretch();
    moreRow->addWidget(m_youtubeMore);
    moreRow->addStretch();
    auto *youtubeBody = new QWidget;
    auto *youtubeColumn = new QVBoxLayout(youtubeBody);
    youtubeColumn->setContentsMargins(0, 0, 0, 0);
    youtubeColumn->setSpacing(10);
    youtubeColumn->addWidget(m_youtubeList);
    youtubeColumn->addLayout(moreRow);
    m_youtubeSection = section("Videos on YouTube", youtubeBody);
    column->addWidget(m_youtubeSection);
    connect(m_youtubeMore, &QPushButton::clicked, this, &SearchPage::moreYouTube);
    // Only the stretch grows: with few results the sections stay together
    // at the top instead of spreading over the page.
    column->addStretch(1);

    m_resultsScroll = new QScrollArea;
    m_resultsScroll->setWidgetResizable(true);
    m_resultsScroll->setFrameShape(QFrame::NoFrame);
    m_resultsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_resultsScroll->setWidget(content);
    m_views->addWidget(m_resultsScroll);

    // A song plays with its radio after it; a video with the other videos
    // found; a playlist opens.
    connect(m_songsList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        playRadio(m_songs.value(item->data(tiles::kEntryRole).toInt()));
    });
    connect(m_videosList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (onPlay)
            onPlay(m_videos, QStringLiteral("Videos: %1").arg(m_query), item->data(tiles::kEntryRole).toInt(), false, false);
    });
    connect(m_youtubeList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (onPlay)
            onPlay(m_ytVideos, QStringLiteral("YouTube: %1").arg(m_query), item->data(tiles::kEntryRole).toInt(), false, false);
    });
    connect(m_playlistsList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const ytmusic::Item p = m_playlists.value(item->data(tiles::kEntryRole).toInt());
        openPlaylist(p.id, p.title);
    });
    connect(m_playlistsList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const ytmusic::Item p = m_playlists.value(item->data(tiles::kEntryRole).toInt());
        openPlaylist(p.id, p.title);
    });
    for (auto [list, items] : {std::pair{m_songsList, &m_songs}, std::pair{m_videosList, &m_videos}, std::pair{m_youtubeList, &m_ytVideos}}) {
        list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(list, &QListWidget::customContextMenuRequested, this, [this, list, items](const QPoint &pos) {
            QListWidgetItem *item = list->itemAt(pos);
            if (!item)
                return;
            if (!item->isSelected()) {
                list->clearSelection();
                item->setSelected(true);
            }
            if (onMenu)
                onMenu(picked(list, *items), list->viewport()->mapToGlobal(pos));
        });
    }

    // -- A playlist -------------------------------------------------------------
    auto *listPage = new QWidget;
    auto *listColumn = new QVBoxLayout(listPage);
    listColumn->setContentsMargins(28, 16, 28, 12);
    listColumn->setSpacing(10);
    auto *back = linkButton(QStringLiteral("‹  Back to results"));
    connect(back, &QPushButton::clicked, this, &SearchPage::showResults);
    auto *backRow = new QHBoxLayout;
    backRow->addWidget(back);
    backRow->addStretch();
    listColumn->addLayout(backRow);
    auto *kicker = new QLabel("PLAYLIST ON YOUTUBE MUSIC");
    kicker->setObjectName("kicker");
    m_listTitle = new QLabel;
    m_listTitle->setObjectName("pageTitle");
    m_listTitle->setWordWrap(true);
    m_listInfo = new QLabel;
    m_listInfo->setObjectName("heroInfo");
    listColumn->addWidget(kicker);
    listColumn->addWidget(m_listTitle);
    listColumn->addWidget(m_listInfo);

    auto *play = new icons::IconButton(icons::Icon::Play, 52, "Play");
    play->setRound(theme::accent(), theme::accentText(), theme::c().onAccent);
    theme::whenChanged(play, [play] { play->setRound(theme::accent(), theme::accentText(), theme::c().onAccent); });
    auto *shuffle = new icons::IconButton(icons::Icon::Shuffle, 38, "Play shuffled");
    m_saveButton = new QPushButton("Save to library");
    m_saveButton->setObjectName("primary");
    m_saveButton->setCursor(Qt::PointingHandCursor);
    m_saveButton->setToolTip("Adds this playlist to your library as a .m3u, to play any time");
    auto *actions = new QHBoxLayout;
    actions->setSpacing(18);
    actions->addWidget(play);
    actions->addWidget(shuffle);
    actions->addSpacing(6);
    actions->addWidget(m_saveButton);
    actions->addStretch();
    listColumn->addLayout(actions);
    connect(play, &QAbstractButton::clicked, this, [this] {
        if (onPlay && !m_list.songs.isEmpty())
            onPlay(m_list.songs, m_list.title, 0, false, true);
    });
    connect(shuffle, &QAbstractButton::clicked, this, [this] {
        if (onPlay && !m_list.songs.isEmpty())
            onPlay(m_list.songs, m_list.title, 0, true, true);
    });
    connect(m_saveButton, &QPushButton::clicked, this, [this] {
        if (onSave && !m_list.songs.isEmpty())
            onSave(m_list.title, m_list.songs);
    });

    m_listSongs = makeList(true);
    listColumn->addWidget(m_listSongs, 1);
    connect(m_listSongs, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (onPlay)
            onPlay(m_list.songs, m_list.title, item->data(tiles::kEntryRole).toInt(), false, true);
    });
    m_listSongs->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listSongs, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_listSongs->itemAt(pos);
        if (!item)
            return;
        if (!item->isSelected()) {
            m_listSongs->clearSelection();
            item->setSelected(true);
        }
        if (onMenu)
            onMenu(picked(m_listSongs, m_list.songs), m_listSongs->viewport()->mapToGlobal(pos));
    });
    m_views->addWidget(listPage);

    // -- YouTube home -----------------------------------------------------------
    auto *homePage = new QWidget;
    auto *homeColumn = new QVBoxLayout(homePage);
    homeColumn->setContentsMargins(20, 16, 12, 0);
    homeColumn->setSpacing(10);
    m_homeChips = new QWidget;
    m_homeChips->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);   // the chips that do not fit hide
    auto *homeChipRow = new QHBoxLayout(m_homeChips);
    homeChipRow->setContentsMargins(8, 0, 0, 0);
    homeChipRow->setSpacing(8);
    homeColumn->addWidget(m_homeChips);
    m_homeStatus = new QLabel;
    m_homeStatus->setObjectName("heroInfo");
    m_homeStatus->setWordWrap(true);
    m_homeStatus->setContentsMargins(8, 0, 0, 0);
    homeColumn->addWidget(m_homeStatus);
    m_homeGrid = new QListWidget;
    m_homeGrid->setObjectName("songs");
    m_homeGrid->setMouseTracking(true);
    m_homeGrid->setViewMode(QListView::IconMode);
    m_homeGrid->setMovement(QListView::Static);
    m_homeGrid->setFlow(QListView::LeftToRight);
    m_homeGrid->setWrapping(true);
    m_homeGrid->setResizeMode(QListView::Fixed);
    m_homeGrid->setUniformItemSizes(true);
    m_homeGrid->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_homeGrid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_homeGrid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_homeGrid->setFrameShape(QFrame::NoFrame);
    m_homeCards = new tiles::VideoCardDelegate([this](const QModelIndex &index) {
        return m_covers.get(index.data(Qt::UserRole).toString(), ThumbnailCache::Size::Card);
    }, m_homeGrid);
    m_homeCards->channelFor = [](const QModelIndex &index) { return index.data(tiles::kSubtitleRole).toString(); };
    // Channel pictures: one small image per channel from YouTube's picture
    // server, once a session, as a browser fetches them on youtube.com.
    m_avatars = new RemotePictures(this);
    m_homeCards->avatarFor = [this](const QModelIndex &index) {
        return m_avatars->get(index.data(tiles::kAvatarRole).toString());
    };
    connect(m_avatars, &RemotePictures::ready, this, [this] { m_homeGrid->viewport()->update(); });
    m_homeGrid->setItemDelegate(m_homeCards);
    // A channel's own page: its header over its videos (Batın, 2026-11-10).
    m_channelHeader = new ChannelHeader(m_avatars);
    m_channelHeader->hide();
    m_channelHeader->onFollow = [this](bool follow) {
        if (onFollow)
            onFollow(m_homeChannelId, m_homeChannelName, follow);
    };
    m_channelHeader->onWheel = [this](QWheelEvent *event) {
        QWheelEvent copy(event->position(), event->globalPosition(), event->pixelDelta(), event->angleDelta(),
                         event->buttons(), event->modifiers(), event->phase(), event->inverted());
        QCoreApplication::sendEvent(m_homeGrid->viewport(), &copy);
    };
    homeColumn->addWidget(m_channelHeader);
    // Scrolled down, the header folds to one row.
    connect(m_homeGrid->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        // Folded past the first row, open again only at the top: the grid
        // growing as it folds does not flip it back.
        if (m_channelHeader->isVisible() && (value > 40 || value == 0) && m_channelHeader->folded() != (value > 40))
            m_channelHeader->setFolded(value > 40, true);
    });
    m_homeGrid->installEventFilter(this);
    m_homeGrid->viewport()->installEventFilter(this);
    homeColumn->addWidget(m_homeGrid, 1);
    m_views->addWidget(homePage);
    connect(m_homeGrid, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (onPlay)
            onPlay(m_home, m_homeChannel.isEmpty() ? QStringLiteral("YouTube") : m_homeChannel,
                   item->data(tiles::kEntryRole).toInt(), false, false);
    });
    m_homeGrid->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_homeGrid, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_homeGrid->itemAt(pos);
        if (!item)
            return;
        if (!item->isSelected()) {
            m_homeGrid->clearSelection();
            item->setSelected(true);
        }
        if (onMenu)
            onMenu(picked(m_homeGrid, m_home), m_homeGrid->viewport()->mapToGlobal(pos));
    });
    // Near the end of what is there, the next videos are fetched.
    connect(m_homeGrid->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value > m_homeGrid->verticalScrollBar()->maximum() - 600)
            homeMore();
    });

    connect(&m_covers, &ThumbnailCache::ready, this, &SearchPage::refreshCovers);
    m_librarySection->hide();
    m_songsSection->hide();
    m_videosSection->hide();
    m_playlistsSection->hide();
    m_youtubeSection->hide();
}

QList<QWidget *> SearchPage::scrollAreas() const
{
    return {m_resultsScroll, m_listSongs, m_homeGrid};
}

QListWidget *SearchPage::makeList(bool scrolls)
{
    auto *list = new QListWidget;
    list->setObjectName("songs");
    list->setMouseTracking(true);
    list->setUniformItemSizes(true);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (!scrolls) {
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // The wheel goes to the page: a list left a few pixels of its own
        // to scroll took the wheel first, and the page stopped, then jumped.
        list->viewport()->installEventFilter(this);
        list->setProperty("pageScrolls", true);
    }
    list->setItemDelegate(new tiles::SongDelegate([this](const QModelIndex &index) {
        return m_covers.get(index.data(Qt::UserRole).toString(), ThumbnailCache::Size::Card);
    }, list));
    // Enter plays what is selected, as a double-click does.
    list->installEventFilter(this);
    return list;
}

void SearchPage::fill(QListWidget *list, const QVector<ytmusic::Item> &items, int shown)
{
    list->clear();
    for (int i = 0; i < qMin(shown, int(items.size())); ++i) {
        const ytmusic::Item &it = items[i];
        auto *row = new QListWidgetItem(it.title);
        QString byline = it.artist;
        if (it.kind == ytmusic::Kind::Video && !it.extra.isEmpty())
            byline += QStringLiteral("  •  ") + it.extra;
        row->setData(tiles::kArtistRole, byline);
        row->setData(tiles::kNumberRole, QString::number(i + 1));
        // A playlist has no length; a space keeps the row's dash away.
        row->setData(tiles::kDurationRole, it.kind == ytmusic::Kind::Playlist ? QStringLiteral(" ")
                                          : it.duration.isEmpty() ? QStringLiteral("LIVE") : it.duration);
        row->setData(tiles::kAddedRole, it.kind == ytmusic::Kind::Song ? (it.album.isEmpty() ? it.extra : it.album) : it.kind == ytmusic::Kind::Playlist ? it.extra : QString());
        row->setData(tiles::kEntryRole, i);
        row->setData(Qt::UserRole, coverKey(it));
        row->setToolTip(it.kind == ytmusic::Kind::Playlist ? QStringLiteral("Click to open") : QStringLiteral("Double-click to play"));
        list->addItem(row);
    }
    if (list != m_listSongs)
        fitHeight(list);
}

void SearchPage::fitHeight(QListWidget *list)
{
    const int row = list->sizeHintForRow(0) > 0 ? list->sizeHintForRow(0) : int(56 * theme::zoom());
    list->setFixedHeight(row * list->count() + 4);
}

QVector<ytmusic::Item> SearchPage::picked(QListWidget *list, const QVector<ytmusic::Item> &items) const
{
    QList<QListWidgetItem *> chosen = list->selectedItems();
    std::sort(chosen.begin(), chosen.end(), [list](QListWidgetItem *a, QListWidgetItem *b) { return list->row(a) < list->row(b); });
    QVector<ytmusic::Item> out;
    for (QListWidgetItem *item : chosen)
        out << items.value(item->data(tiles::kEntryRole).toInt());
    return out;
}

void SearchPage::setStatus(const QString &text)
{
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
}

void SearchPage::updateEmpty()
{
    if (m_pending > 0)
        return;
    const bool nothing = m_songs.isEmpty() && m_videos.isEmpty() && m_playlists.isEmpty() && m_ytVideos.isEmpty();
    // Nothing came because nothing answered: say so, with a way to ask again,
    // rather than "nothing found" (the computer had just started, 2026-11-09).
    if (nothing && m_client.unreachable()) {
        setStatus(QStringLiteral("Can't reach %1 right now. Check your connection, then "
                                 "<a href=\"retry\" style=\"color: %2;\">try again</a>.")
                      .arg(m_youtube ? QStringLiteral("YouTube") : QStringLiteral("YouTube Music"),
                           theme::accentText().name()));
        return;
    }
    if (m_youtube) {
        setStatus(nothing ? QStringLiteral("Nothing found on YouTube for “%1”. If every search says this, "
                                           "YouTube may be out of reach from here.").arg(m_query)
                          : QString());
        return;
    }
    setStatus(nothing ? QStringLiteral("Nothing found on YouTube Music for “%1”. If every search says this, "
                                       "YouTube Music may be out of reach from here.").arg(m_query)
                      : QString());
}

void SearchPage::setQuery(const QString &text)
{
    const QString query = text.trimmed();
    if (query == m_query && !query.isEmpty())
        return;
    m_query = query;
    const int serial = ++m_serial;
    showResults();
    m_songs.clear();
    m_videos.clear();
    m_playlists.clear();
    m_ytVideos.clear();
    m_ytNext.clear();
    m_pending = 0;
    for (QListWidget *list : {m_songsList, m_videosList, m_playlistsList, m_youtubeList})
        list->clear();
    m_songsSection->hide();
    m_videosSection->hide();
    m_playlistsSection->hide();
    m_youtubeSection->hide();
    if (query.isEmpty()) {
        setStatus({});
        m_librarySection->hide();
        return;
    }

    noteView(QStringLiteral("results:") + query, [this, query] {
        if (m_youtube)
            setYouTubeMode(false);
        if (m_query == query)
            showResults();
        else
            setQuery(query);
    });

    // A pasted link: its playlist opens; a song or video link shows that
    // one, ready to play with its radio.
    const QString listId = ytmusic::playlistIdIn(query);
    const QString videoId = ytmusic::videoIdIn(query);
    m_librarySection->hide();
    if (!listId.isEmpty()) {
        setStatus({});
        m_listFromLink = true;
        openPlaylist(listId, QString());
        return;
    }
    if (query.contains(QLatin1String("open.spotify.com"))) {
        setStatus("Spotify links can't be read here yet. Paste it to the Ghost Engine bot on Discord, then !export: "
                  "the .m3u it gives you lands in your library.");
        return;
    }
    if (!videoId.isEmpty()) {
        setStatus("Finding the song...");
        m_pending = 1;
        m_client.radio(videoId, [this, serial](QVector<ytmusic::Item> items) {
            if (serial != m_serial)
                return;
            m_pending = 0;
            if (!items.isEmpty()) {
                m_songs = {items.first()};
                fill(m_songsList, m_songs, 1);
                m_songsMore->hide();
                m_songsSection->show();
            }
            updateEmpty();
        });
        return;
    }
    if (query.startsWith(QLatin1String("http"), Qt::CaseInsensitive)) {
        // A link with nothing to open here; searching its text finds nonsense.
        setStatus("This link can't be opened here. Paste a YouTube or YouTube Music link to a song or a playlist. "
                  "Mixes made for one viewer (“My mix”) can't be read.");
        return;
    }

    if (m_youtube) {
        searchInGrid(query);
        return;
    }
    // Lists in the library whose name matches, as chips.
    QLayout *chips = m_libraryChips->layout();
    while (QLayoutItem *child = chips->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    const QVector<LibraryMatch> matches = libraryMatches ? libraryMatches(query) : QVector<LibraryMatch>();
    for (int i = 0; i < qMin(6, int(matches.size())); ++i) {
        auto *chip = new QPushButton(matches[i].name);
        chip->setObjectName("chip");
        chip->setCursor(Qt::PointingHandCursor);
        const QString path = matches[i].path;
        connect(chip, &QPushButton::clicked, this, [this, path] { if (onOpenLibrary) onOpenLibrary(path); });
        chips->addWidget(chip);
    }
    static_cast<QHBoxLayout *>(chips)->addStretch();
    m_librarySection->setVisible(!matches.isEmpty());

    setStatus("Searching YouTube Music...");
    m_pending = 3;
    m_client.search(query, ytmusic::Kind::Song, [this, serial](QVector<ytmusic::Item> items) {
        if (serial != m_serial)
            return;
        --m_pending;
        m_songs = items;
        fill(m_songsList, m_songs, kSongsShown);
        m_songsMore->setText("Show all");
        m_songsMore->setVisible(m_songs.size() > kSongsShown);
        m_songsSection->setVisible(!m_songs.isEmpty());
        updateEmpty();
    });
    m_client.search(query, ytmusic::Kind::Video, [this, serial](QVector<ytmusic::Item> items) {
        if (serial != m_serial)
            return;
        --m_pending;
        m_videos = items;
        fill(m_videosList, m_videos, kVideosShown);
        m_videosMore->setText("Show all");
        m_videosMore->setVisible(m_videos.size() > kVideosShown);
        m_videosSection->setVisible(!m_videos.isEmpty());
        updateEmpty();
    });
    m_client.search(query, ytmusic::Kind::Playlist, [this, serial](QVector<ytmusic::Item> items) {
        if (serial != m_serial)
            return;
        --m_pending;
        m_playlists = items.mid(0, kPlaylistsShown);
        fill(m_playlistsList, m_playlists, kPlaylistsShown);
        m_playlistsSection->setVisible(!m_playlists.isEmpty());
        updateEmpty();
    });
}

void SearchPage::setYouTubeMode(bool youtube)
{
    m_musicMode->setChecked(!youtube);
    m_youtubeMode->setChecked(youtube);
    if (youtube == m_youtube)
        return;
    m_youtube = youtube;
    QSettings().setValue("search/youtube", youtube);
    const QString query = m_query;
    m_query.clear();
    setQuery(query);
}

void SearchPage::showYouTubeHome()
{
    showChannelHeader(false);
    if (!m_youtube)
        setYouTubeMode(true);
    m_views->setCurrentWidget(m_homeGrid->parentWidget());
    // The chips: All, then the channels watched most.
    QLayout *chips = m_homeChips->layout();
    while (QLayoutItem *child = chips->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    QStringList names{QString()};
    if (watchedChannels)
        names += watchedChannels().mid(0, 10);
    for (const QString &name : names) {
        auto *chip = new QPushButton(name.isEmpty() ? QStringLiteral("All") : name);
        chip->setObjectName("chip");
        chip->setCheckable(true);
        chip->setChecked(name == m_homeChannel);
        chip->setCursor(Qt::PointingHandCursor);
        connect(chip, &QPushButton::clicked, this, [this, name] { homeChip(name); });
        chips->addWidget(chip);
    }
    static_cast<QHBoxLayout *>(chips)->addStretch();
    if (m_homeFixed || m_homeSearch || !m_homeChannelId.isEmpty()) {
        m_homeFixed = false;
        m_homeSearch = false;
        m_homeChannelId.clear();
        m_homeChannel.clear();
        for (QPushButton *chip : m_homeChips->findChildren<QPushButton *>())
            chip->setChecked(chip->text() == QLatin1String("All"));
        fillHome(true);
    } else if (m_home.isEmpty() && !m_homeLoading) {
        fillHome(true);
    }
    QTimer::singleShot(0, this, &SearchPage::fitHomeGrid);
    noteView(QStringLiteral("ythome"), [this] { showYouTubeHome(); });
}

void SearchPage::showVideos(const QString &title, const QVector<ytmusic::Item> &items)
{
    showYouTubeHome();
    ++m_homeSerial;
    m_homeFixed = true;
    m_homeSearch = false;
    m_homeChannelId.clear();
    m_homeLoading = false;
    m_home.clear();
    m_homeSeen.clear();
    m_homeGrid->clear();
    for (QPushButton *chip : m_homeChips->findChildren<QPushButton *>())
        chip->setChecked(false);
    m_homeStatus->setText(title);
    m_homeStatus->show();
    appendHome(items);
    m_homeGrid->verticalScrollBar()->setValue(0);    // "Following: loading..." and "Following" are one place.
    noteView(QStringLiteral("videos:") + title.section(QLatin1Char(':'), 0, 0),
             [this, title, items] { showVideos(title, items); });
}

int SearchPage::appendHome(const QVector<ytmusic::Item> &items)
{
    int added = 0;
    for (const ytmusic::Item &it : items) {
        if (m_homeSeen.contains(it.id))
            continue;
        m_homeSeen.insert(it.id);
        const int i = m_home.size();
        m_home << it;
        auto *row = new QListWidgetItem(it.title);
        QString byline = it.artist;
        if (!it.extra.isEmpty())
            byline += QStringLiteral("  \u00b7  ") + it.extra;
        row->setData(tiles::kSubtitleRole, byline);
        row->setData(tiles::kDurationRole, it.duration.isEmpty() && !m_homeFixed ? QStringLiteral("LIVE") : it.duration);
        row->setData(tiles::kEntryRole, i);
        row->setData(Qt::UserRole, it.id);
        QString avatar = it.avatar.startsWith(QLatin1String("//")) ? QStringLiteral("https:") + it.avatar : it.avatar;
        if (!avatar.isEmpty() && !it.channelId.isEmpty())
            m_avatarOf.insert(it.channelId, avatar);
        if (avatar.isEmpty())
            avatar = m_avatarOf.value(it.channelId);
        row->setData(tiles::kAvatarRole, avatar);
        row->setToolTip(it.title + QStringLiteral("\n") + byline);
        m_homeGrid->addItem(row);
        ++added;
    }
    return added;
}

void SearchPage::searchInGrid(const QString &query)
{
    showChannelHeader(false);
    m_homeChannelId.clear();
    // YouTube's results as its home's cards (channel pictures, length,
    // views), page after page as it is scrolled, the chips above.
    m_views->setCurrentWidget(m_homeGrid->parentWidget());
    ++m_homeSerial;
    m_homeFixed = false;
    m_homeSearch = true;
    m_homeLoading = false;
    m_home.clear();
    m_homeSeen.clear();
    m_homeSeeded.clear();
    m_homeSeeds.clear();
    m_homeNext.clear();
    m_homeGrid->clear();
    m_homeChannel = query;
    for (QPushButton *chip : m_homeChips->findChildren<QPushButton *>())
        chip->setChecked(false);
    m_homeStatus->setText(QStringLiteral("Results for \u201c%1\u201d").arg(query));
    m_homeStatus->show();
    m_homeGrid->verticalScrollBar()->setValue(0);
    homeMore();
    QTimer::singleShot(0, this, &SearchPage::fitHomeGrid);
    noteView(QStringLiteral("grid:") + query, [this, query] {
        if (!m_youtube)
            setYouTubeMode(true);
        searchInGrid(query);
    });
}

void SearchPage::showChannel(const QString &channelId, const QString &name)
{
    if (channelId.isEmpty())
        return;
    if (m_views->currentWidget() != m_homeGrid->parentWidget())
        showYouTubeHome();
    m_views->setCurrentWidget(m_homeGrid->parentWidget());
    ++m_homeSerial;
    m_homeFixed = false;
    m_homeSearch = false;
    m_homeLoading = false;
    m_home.clear();
    m_homeSeen.clear();
    m_homeSeeded.clear();
    m_homeSeeds.clear();
    m_homeNext.clear();
    m_homeGrid->clear();
    m_homeChannel.clear();
    m_homeChannelId = channelId;
    m_homeChannelName = name;
    for (QPushButton *chip : m_homeChips->findChildren<QPushButton *>())
        chip->setChecked(false);
    m_homeStatus->hide();
    m_channelHeader->reset(channelId, name);
    m_channelHeader->setFollowed(isFollowed && isFollowed(channelId));
    showChannelHeader(true);
    m_homeGrid->verticalScrollBar()->setValue(0);
    homeMore();
    // On air now: its stream leads the videos, the picture ringed in red.
    const int liveSerial = m_homeSerial;
    m_client.liveNow(channelId, [this, liveSerial](const ytmusic::Item &live) {
        if (liveSerial != m_homeSerial || live.id.isEmpty())
            return;
        m_channelHeader->setLive(true);
        ytmusic::Item first = live;
        first.artist = m_homeChannelName;
        QVector<ytmusic::Item> items{first};
        for (const ytmusic::Item &it : std::as_const(m_home))
            if (it.id != live.id)
                items << it;
        m_home.clear();
        m_homeSeen.clear();
        m_homeGrid->clear();
        appendHome(items);
    });
    QTimer::singleShot(0, this, &SearchPage::fitHomeGrid);
    if (onChannelShown)
        onChannelShown(channelId, name);
    noteView(QStringLiteral("channel:") + channelId, [this, channelId, name] { showChannel(channelId, name); });
}

void SearchPage::homeChip(const QString &channel)
{
    showChannelHeader(false);
    m_homeChannelId.clear();
    m_homeSearch = false;
    m_homeFixed = false;
    m_homeChannel = channel;
    for (QPushButton *chip : m_homeChips->findChildren<QPushButton *>())
        chip->setChecked(chip->text() == (channel.isEmpty() ? QStringLiteral("All") : channel));
    fillHome(true);
}

void SearchPage::fillHome(bool reset)
{
    if (reset) {
        ++m_homeSerial;
        m_home.clear();
        m_homeSeen.clear();
        m_homeSeeded.clear();
        m_homeNext.clear();
        m_homeGrid->clear();
        m_homeLoading = false;
        m_homeSeeds = watchedVideos ? watchedVideos() : QStringList();
        for (const QString &id : m_homeSeeds)
            m_homeSeen.insert(id);   // watched already: not offered again
        m_homeSeeds = m_homeSeeds.mid(0, 12);
        m_homeGrid->verticalScrollBar()->setValue(0);
    }
    if (!m_homeChannel.isEmpty() || m_homeSeeds.isEmpty()) {
        // A chip, or nothing watched yet: a YouTube search fills the grid.
        QString query = m_homeChannel;
        if (query.isEmpty()) {
            const QStringList channels = watchedChannels ? watchedChannels() : QStringList();
            query = channels.isEmpty() ? QStringLiteral("popular videos") : channels.first();
        }
        m_homeStatus->setText(reset && m_homeChannel.isEmpty() && m_homeSeeds.isEmpty() && m_home.isEmpty()
                                  ? QStringLiteral("Watch a few videos here and this page fills with what YouTube "
                                                   "shows beside them.")
                                  : QString());
        m_homeStatus->setVisible(!m_homeStatus->text().isEmpty());
        homeMore();
        return;
    }
    m_homeStatus->hide();
    homeMore();
}

void SearchPage::homeMore()
{
    if (m_homeFixed || m_homeLoading || (!m_homeGrid->isVisible() && !m_home.isEmpty()))
        return;
    const int serial = m_homeSerial;
    const auto add = [this, serial](const QVector<ytmusic::Item> &items) {
        if (serial != m_homeSerial)
            return false;
        m_homeLoading = false;
        return appendHome(items) > 0;
    };

    // A channel's page: its Videos tab, page after page.
    if (!m_homeChannelId.isEmpty()) {
        if (!m_home.isEmpty() && m_homeNext.isEmpty())
            return;
        m_homeLoading = true;
        m_client.channelVideos(m_homeChannelId, m_homeNext, [this, add, serial](ytmusic::ChannelPage page) {
            if (serial != m_homeSerial)
                return;
            m_homeNext = page.next;
            if (!page.name.isEmpty()) {
                m_homeChannelName = page.name;
                m_homeStatus->setText(page.about.isEmpty() ? page.name : page.name + QStringLiteral("  \u00b7  ") + page.about);
                m_channelHeader->setPage(page);
                const QString channel = m_homeChannelId;
                m_client.channelAbout(page.aboutToken, [this, channel](const ytmusic::ChannelAbout &about) {
                    if (channel == m_homeChannelId)
                        m_channelHeader->setAbout(about);
                });
            }
            for (ytmusic::Item &it : page.videos) {
                it.artist = m_homeChannelName;
                if (it.avatar.isEmpty())
                    it.avatar = page.avatar.isEmpty() ? m_avatarOf.value(m_homeChannelId) : page.avatar;
            }
            add(page.videos);
        });
        return;
    }
    const bool search = !m_homeChannel.isEmpty() || (m_homeSeeds.isEmpty() && m_home.isEmpty())
                        || (m_homeSeeds.isEmpty() && !m_homeNext.isEmpty());
    if (search) {
        if (!m_home.isEmpty() && m_homeNext.isEmpty())
            return;   // the search's last page is there
        QString query = m_homeChannel;
        if (query.isEmpty()) {
            const QStringList channels = watchedChannels ? watchedChannels() : QStringList();
            query = channels.isEmpty() ? QStringLiteral("popular videos") : channels.first();
        }
        m_homeLoading = true;
        m_client.searchYouTube(query, m_homeNext, [this, add, serial](QVector<ytmusic::Item> items, QString next) {
            if (serial != m_homeSerial)
                return;
            m_homeNext = next;
            add(items);
        });
        return;
    }
    // All: what YouTube shows beside the next watched video; once those are
    // used, beside the videos the grid itself shows, so it goes on.
    if (m_homeSeeds.isEmpty())
        for (int i = 0; i < m_home.size() && m_homeSeeds.size() < 6; i += 4)
            if (!m_homeSeeded.contains(m_home[i].id))
                m_homeSeeds << m_home[i].id;
    if (m_homeSeeds.isEmpty())
        return;
    const QString seed = m_homeSeeds.takeFirst();
    m_homeSeeded.insert(seed);
    m_homeLoading = true;
    m_client.related(seed, [this, add](QVector<ytmusic::Item> items) {
        // Few new ones (a side list much like the last): ask beside the next.
        if (!add(items) || m_homeGrid->count() < 12)
            QTimer::singleShot(0, this, &SearchPage::homeMore);
    });
}

void SearchPage::fitHomeGrid()
{
    // As many columns as fit at 360 px or more (YouTube's own: 4 across a
    // 1920 px window with its menu open; Batın, 2026-11-08: 5 felt crowded),
    // sharing the width. The
    // scroll bar's room is always kept: measured with and without it, 15
    // videos fit in 5 columns without the bar, the bar went, the width grew
    // to 5 columns... and the grid flicked between 4 and 5 for ever (seen).
    const int bar = m_homeGrid->verticalScrollBar()->isVisible()
                        ? 0 : m_homeGrid->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, m_homeGrid->verticalScrollBar());
    const int width = m_homeGrid->viewport()->width() - bar - 8;
    if (width <= 0)
        return;
    const int columns = qMax(1, width / int(360 * theme::zoom()));
    m_homeCards->setCardWidth(width / columns);
    // The chips at their own width, as many as fit, in order: squeezed, their
    // names were cut ("Coffee C", "es Batu" at 160%, 2026-11-10).
    int room = m_homeChips->width() - 16, used = 0;
    bool full = false;
    for (QPushButton *chip : m_homeChips->findChildren<QPushButton *>(Qt::FindDirectChildrenOnly)) {
        const int w = chip->sizeHint().width();
        chip->setMinimumWidth(w);
        full = full || used + w > room;
        chip->setVisible(!full);
        used += w + 8;
    }
    if (m_homeGrid->gridSize() != m_homeCards->cardSize())
        m_homeGrid->setGridSize(m_homeCards->cardSize());
    m_homeGrid->doItemsLayout();
}

void SearchPage::moreYouTube()
{
    if (m_ytNext.isEmpty())
        return;
    m_youtubeMore->setText("Loading...");
    m_youtubeMore->setEnabled(false);
    const int serial = m_serial;
    m_client.searchYouTube(m_query, m_ytNext, [this, serial](QVector<ytmusic::Item> items, QString next) {
        if (serial != m_serial)
            return;
        // A later page repeats a video now and then; each shows once.
        QSet<QString> seen;
        for (const ytmusic::Item &it : m_ytVideos)
            seen.insert(it.id);
        for (const ytmusic::Item &it : items)
            if (!seen.contains(it.id))
                m_ytVideos << it;
        m_ytNext = next;
        fill(m_youtubeList, m_ytVideos, m_ytVideos.size());
        m_youtubeMore->setText("Show more");
        m_youtubeMore->setEnabled(true);
        m_youtubeMore->setVisible(!next.isEmpty());
    });
}

void SearchPage::playRadio(const ytmusic::Item &song)
{
    if (song.id.isEmpty() || !onPlay)
        return;
    if (onToast)
        onToast(QStringLiteral("Starting %1 radio...").arg(song.title));
    m_client.radio(song.id, [this, song](QVector<ytmusic::Item> items) {
        // The song itself leads, whatever the radio put first.
        QVector<ytmusic::Item> queue{song};
        for (const ytmusic::Item &it : items)
            if (it.id != song.id)
                queue << it;
        onPlay(queue, QStringLiteral("%1 radio").arg(song.title), 0, false, true);
    });
}

void SearchPage::showResults()
{
    m_views->setCurrentIndex(0);
}

void SearchPage::openPlaylist(const QString &id, const QString &title)
{
    m_list = {};
    m_list.id = id;
    m_listTitle->setText(title.isEmpty() ? QStringLiteral("Loading...") : title);
    m_listInfo->setText("Loading the songs...");
    m_listSongs->clear();
    m_saveButton->setEnabled(false);
    m_views->setCurrentIndex(1);
    noteView(QStringLiteral("ytlist:") + id, [this, id, title] { openPlaylist(id, title); });
    const int serial = m_serial;
    m_client.playlist(id, [this, serial, title](ytmusic::PlaylistPage page) {
        if (serial != m_serial || page.id != m_list.id)
            return;
        if (page.title.isEmpty())
            page.title = title.isEmpty() ? QStringLiteral("YouTube playlist") : title;
        m_list = page;
        m_listTitle->setText(page.title);
        if (!page.ok) {
            m_listInfo->setText("This playlist couldn't be read. It may be private, or a mix made for one viewer.");
            return;
        }
        m_listInfo->setText(page.subtitle.isEmpty() ? QStringLiteral("%1 songs").arg(page.songs.size()) : page.subtitle);
        fill(m_listSongs, page.songs, page.songs.size());
        m_saveButton->setEnabled(true);
    });
}

bool SearchPage::eventFilter(QObject *watched, QEvent *event)
{
    if (m_homeGrid && watched == m_homeGrid->viewport() && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, &SearchPage::fitHomeGrid);
    // The channel's picture on a card opens the channel, as on YouTube. Its
    // place is the card delegate's: under the 16:9 thumbnail, at the left.
    if (m_homeGrid && watched == m_homeGrid->viewport() && event->type() == QEvent::MouseButtonRelease &&
        static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
        const QPoint at = static_cast<QMouseEvent *>(event)->position().toPoint();
        if (QListWidgetItem *item = m_homeGrid->itemAt(at)) {
            const QRectF card = QRectF(m_homeGrid->visualItemRect(item)).adjusted(8, 8, -8, -4);
            const qreal size = qRound(34 * theme::zoom());
            const QRectF circle(card.left() + 2, card.top() + card.width() * 9 / 16 + 10, size, size);
            const ytmusic::Item it = m_home.value(item->data(tiles::kEntryRole).toInt());
            if (circle.adjusted(-4, -4, 4, 4).contains(at) && !it.channelId.isEmpty()) {
                showChannel(it.channelId, it.artist);
                return true;
            }
        }
    }
    if (event->type() == QEvent::Wheel && watched->parent() && watched->parent()->property("pageScrolls").toBool()) {
        QCoreApplication::sendEvent(m_resultsScroll->viewport(), event);
        return true;
    }
    auto *list = qobject_cast<QListWidget *>(watched);
    if (list && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && list->currentItem()) {
            emit list->itemDoubleClicked(list->currentItem());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SearchPage::refreshCovers(const QString &)
{
    for (QListWidget *list : {m_songsList, m_videosList, m_playlistsList, m_youtubeList, m_listSongs, m_homeGrid})
        if (list->isVisible())
            list->viewport()->update();
}

void SearchPage::noteView(const QString &key, std::function<void()> go)
{
    m_viewKey = key;
    m_viewGo = std::move(go);
    if (onViewChanged)
        onViewChanged();
}

void SearchPage::showChannelHeader(bool shown)
{
    m_channelHeader->setVisible(shown);
    m_homeChips->setVisible(!shown);
}

void SearchPage::refreshFollow()
{
    if (m_channelHeader && !m_homeChannelId.isEmpty())
        m_channelHeader->setFollowed(isFollowed && isFollowed(m_homeChannelId));
}
