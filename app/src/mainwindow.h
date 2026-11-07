#pragma once

#include "library.h"
#include "lyrics.h"
#include "offline.h"
#include "profile.h"
#include "mpvclient.h"
#include "nowplayingpanel.h"
#include "resolver.h"
#include "searchpage.h"
#include "shortcuts.h"
#include "thumbnails.h"
#include "youtube.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPixmap>
#include <QPointer>
#include <QFileSystemWatcher>
#include <QSet>
#include <optional>
#include <QMainWindow>
#include <QTimer>

class NowPlayingBar;
class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QWidget;
namespace tiles { class RowDelegate; class VideoCardDelegate; }

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

    // Lists opened with the app (double-clicked .m3u files): moved into the
    // library and shown, the way osu! takes in a skin.
    void importFiles(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    bool dragOrResize(QObject *watched, QEvent *event);
    bool m_edgeCursor = false;   // the resize cursor is shown at a window edge

private:
    enum Filter { AllLists, Songs, Videos };
    // How the open list's songs are shown. Custom is the file's own order,
    // the only one a drag can change.
    enum class SortKey { Custom, Title, Artist, Added, Duration, Shuffled };

    QWidget *buildTopBar();
    QWidget *buildLibrary();
    bool m_youtubeLayout = false;
    class QStackedWidget *m_leftStack = nullptr;
    QPushButton *m_youtubeButton = nullptr;
    QList<QPushButton *> m_ytNav;   // Home, History
    class QListWidget *m_ytLists = nullptr;
    QWidget *buildHome();
    QWidget *buildPlaylistPage();
    QListWidget *makeShelf();

    void rescan();
    void refill();
    void fillShelf(QListWidget *shelf, const QVector<int> &indices);
    void openPlaylist(int index, bool keepView = false);
    void sortBy(SortKey key);
    void applySort();
    void filterSongs();
    QVector<int> shownOrder() const;
    void saveSongOrder();
    void removeSelectedSongs();
    void addSelectedSongsTo(const QString &path);
    void newPlaylist(const QString &folder = QString(), bool addSelected = false);
    void toast(const QString &text, int ms = 2600);
    void songFailed(int entryId);
    // Updates (update.ps1 -Check once a day, or asked for) and the tray.
    void checkForUpdates(bool asked);
    void showUpdates();
    void buildTray();
    void updateTray();
    void showThanks();
    void quitForReal();

public:
    void bringToFront();

private:
    void undoSongEdit();
    void applyPendingOrder();
    void showHome();
    void showYouTube();
    // Two apps in one: the music layout (library, lyrics, artists) and the
    // YouTube one (its own menu, home, channels). The top bar's video button
    // goes from one to the other, the new one sliding in.
    void setYouTubeLayout(bool youtube, bool animate);
    QWidget *buildYouTubeSidebar();
    void fillYouTubeSidebar();
    void showVideoHistory();
    void showProfile();
    void showSettings();
    void showShortcuts();
    void runShortcut(const QString &action);
    void toggleLyrics();
    // The song mpv is on, as artist and track for a lyrics search.
    std::pair<QString, QString> playingSong() const;
    void lookUpLyrics();
    void tintLyrics();
    void applyLook();
    // Song lists play with no mpv window when the user chose so (Settings).
    void sendWindowChoice();
    void markPlaying();
    void layoutHeader();
    void fitTitle();
    void placeNameEdit();
    static QString titleSheet(qreal points, bool hidden);
    qreal m_titlePt = 40;
    void renamePlaylist(int index);
    void finishRename();
    void renameTo(int index, const QString &name);
    void movedPlayed(const QString &from, const QString &to);
    void importStray(bool ask);
    void fillDurations();
    void openPlayingList();
    void exportPlaylist(int index);
    void changeZoom(qreal step);
    static QString relativeTime(const QDateTime &when);
    QString videoByline(const QString &id);
    void deletePlaylist(int index);
    void reopenAfter(const QString &path);
    void syncSearchWidth();
    void fitLibraryHead();
    void foldHeader();
    QString memoryUsage() const;
    void tieToApp(qint64 pid);
    int headerTallNow() const;
    void snapCompactLibrary();
    QPixmap grabStill() const;
    bool panelFits() const;
    int compactBelow() const;
    void setLibraryCompact(bool compact);
    // `order`: the entries in the order to play them, when the list is shown
    // sorted; empty plays the file's order.
    void play(const QString &path, bool shuffle = false, int startAt = 0, const QVector<int> &order = {});
    void playOpen(int startAt, bool shuffle = false);
    void addFolder();
    void showLibraryMenu(const QPoint &pos);
    // A playlist's menu, from its row in the library or its cover on its page.
    void showPlaylistMenu(int index, const QPoint &at);
    void setLibraryDense(bool dense);
    void newFolder(const QString &thenMove = QString());
    void moveToFolder(const QString &path, const QString &folder);
    void renameFolder(const QString &folder);
    void deleteFolder(const QString &folder);
    void saveOpenFolders();
    // The library panel's own order: pinned rows first, rows nobody placed
    // yet (new ones) next, then the order the last drag left.
    QStringList arranged(const QStringList &keys, const QString &level) const;
    void libraryDrop(const QStringList &moving, const QString &targetKey, int position);
    void togglePin(const QString &key);
    void renameKey(const QString &from, const QString &to);

    QVector<QPixmap> coversFor(int index);
    // A picture chosen for a list replaces its collage. Kept apart from the
    // .m3u, under the list's file name, which a move or rename keeps.
    static QString customCoverPath(const QString &playlistPath);
    void chooseCover(int index);
    void removeCover(int index);
    QStringList coverIds(const Playlist &p, int wanted);
    QString idForTarget(const QString &target);
    QString findMpv() const;
    // The file of the list mpv is playing. A list moved into a folder while
    // playing keeps its file name, so it is found by that.
    QString playingListPath() const;
    void rememberPlayed(const QString &path);
    // The top search box: the search page after a short pause in typing,
    // and back to the page before it when the box is emptied.
    void runSearch();
    void playFound(const QVector<ytmusic::Item> &items, const QString &name, int start, bool shuffle, bool audio);
    void saveFound(const QString &name, const QVector<ytmusic::Item> &items);
    void foundMenu(const QVector<ytmusic::Item> &items, const QPoint &global);
    static QString foundFolder();

    Library m_library;
    ThumbnailCache m_covers;
    youtube::Matches m_matches;
    CoverResolver m_resolver;
    MpvClient m_mpv;
    Filter m_filter = AllLists;
    int m_open = -1;              // playlist shown on the playlist page
    bool m_compact = false;

    // window chrome
    QWidget *m_topBar = nullptr;
    QPushButton *m_maxButton = nullptr;
    QList<QWidget *> m_chromeButtons;

    // library panel
    QSplitter *m_split = nullptr;
    QWidget *m_libraryPanel = nullptr;
    QWidget *m_libraryHead = nullptr;
    QLabel *m_libraryTitle = nullptr;
    QPushButton *m_collapse = nullptr;
    QPushButton *m_viewToggle = nullptr;
    QPushButton *m_addButton = nullptr;
    QHash<QString, QPixmap> m_customCovers;   // by customCoverPath(); a null one: none
    QSet<QString> m_openFolders;   // library folders showing their playlists
    // Each list's song titles ("artist - track"), read once per file date,
    // so the library search finds songs without opening hundreds of files
    // on every key.
    QHash<QString, QPair<QDateTime, QStringList>> m_titleCache;
    QLabel *m_libraryEmpty = nullptr;   // over the library while a search finds nothing
    QString m_lastToggled;         // a double-click is two clicks; see buildLibrary
    QElapsedTimer m_toggleClock;
    QWidget *m_chipRow = nullptr;
    QButtonGroup *m_chips = nullptr;
    QLineEdit *m_search = nullptr;
    QLineEdit *m_librarySearch = nullptr;
    QPushButton *m_libraryFind = nullptr;
    QListWidget *m_libraryList = nullptr;
    tiles::RowDelegate *m_rowDelegate = nullptr;

    // pages
    QStackedWidget *m_pages = nullptr;
    struct QueuedPlay { QString path; bool shuffle = false; int startAt = 0; QVector<int> order; };
    class SettingsPage *m_settings = nullptr;
    class VideoPage *m_videoPage = nullptr;
    // Video lists play inside the app: mpv draws into the video page (it is
    // started with --wid). One mpv still; it is started again, at the same
    // song and second, when it has to move in or out of the app.
    bool m_mpvEmbedded = false;     // the mpv talked to draws into the page
    bool m_launchEmbedded = false;  // the one being started will
    bool m_videoPoppedOut = false;  // moved to mpv's own window by the user
    bool m_videoFull = false;
    bool m_wasMaximized = false;
    int m_libraryWidthBeforeFull = 300;
    bool m_compactBeforeFull = false;
    QElapsedTimer m_restoreLibraryUntil;
    void restoreLibraryWidth();
    double m_startSeconds = 0;      // where the next started mpv begins
    std::optional<QueuedPlay> m_restartPlay;
    QString m_videoListPath;
    bool m_resetStart = false;       // a quality change reloaded at a second
    QHash<QString, QString> m_descriptions;   // video id -> its description
    QString m_describing;            // the id being fetched
    void describeVideo(const QString &id);
    QHash<QString, QString> m_songInfo;   // a song's entry -> what yt-dlp printed about it
    void showSongInfo(const QString &target, const QString &title, const QString &artist);
    void fetchSongInfo(const QString &key, const QString &url, std::function<void(const QString &printed)> done);

    // The now playing view at the right, beside the page.
    class NowPlayingPanel *m_panel = nullptr;
    class QSplitter *m_rightSplit = nullptr;
    ArtistLookup m_artists;

    // The profile page: who is listening (from Discord), what plays now, and
    // what has been played most.
    DiscordProfile m_profile;
    RemotePictures m_pictures;
    // Sharing: a copy of the list with the profile card in it, put on the
    // clipboard as a file to paste into a Discord chat.
    void shareList(int index);
    QWidget *m_sharedBy = nullptr;   // "Shared by" under a received list's name
    QLabel *m_sharedAvatar = nullptr;
    QLabel *m_sharedText = nullptr;
    QString m_sharedAvatarUrl;
    void showSharedBy(const QString &tag);
    ListeningStats m_stats;
    int m_historyShown = 20;      // rows of History on the profile; "Show more" adds 50
    bool m_videosSorted = false;  // old song counts that were videos, moved over once
    ProfilePage *m_profilePage = nullptr;
    QWidget *m_profileScroll = nullptr;
    SearchPage *m_searchPage = nullptr;
    QWidget *m_shortcutsScroll = nullptr;
    QWidget *m_beforeSearch = nullptr;
    QTimer m_searchTimer;
    QListWidget *m_topLists = nullptr;
    class QPushButton *m_profileButton = nullptr;
    QTimer m_profileSoon;
    QTimer m_resumeSave;
    void refreshProfile();
    // "Pick up where you left off": where mpv is, kept every few seconds and
    // on quitting, then shown in the bar while nothing plays.
    void saveResume();
    void showResume();
    void resume();
    void refreshProfileUser();
    // A song counts as played after 30 seconds of it.
    void trackListening();
    QString m_listenPath;
    double m_listenLast = 0;
    double m_listenSeconds = 0;
    bool m_listenCounted = false;
    Lyrics m_lyrics;                 // the playing song's, once found
    bool m_panelOpen = true;
    QString m_panelFacts;            // the video whose credits the panel shows
    QString m_panelChannel;          // and its channel, the artist's
    QTimer m_panelSoon;
    void refreshPanel();
    void updatePanelVisibility();
    // A song of a song list played as its music video, and back.
    bool songVideo() const;
    void setSongVideo(bool on);
    // "Mini video": this one song's video in the now playing view, where its
    // cover was; the next song is sound again.
    bool panelVideo() const;
    void setPanelVideo(bool on);
    bool m_findingMini = false;
    QString m_miniPath;              // the song "Mini video" was asked for
    bool m_fullFromPanel = false;    // full screen asked for from the view
    void songVideoChanged();
    bool m_findingVideo = false;     // asked for, not playing yet
    bool m_wantVideoPage = false;
    bool listIsAudio(const QString &path) const;
    int embedFor(bool audio) const;   // 1 inside the app, 0 outside, -1 either
    void restartPlayer(const QueuedPlay &q, double seconds);
    void showVideoPage();
    void updateVideoPage();
    // mpv's window over the video page's screen, or small in the corner (the
    // mini player) while a video plays and another page is open; the bar of
    // controls over it shows while the mouse moves on the picture.
    void placeSurface();
    void placeWithPage();
    // While a panel edge is dragged mpv's window is put away and the
    // thumbnail shows in its place: resized on every step, mpv held up the
    // drag (its window shares this window's input queue).
    void startSurfaceDrag();
    bool m_surfaceDrag = false;
    QPixmap m_pressedStill;
    QRect m_normalPlace;
    void keepNormalPlaceOnScreen();
    int m_libraryWanted = -1;
    int m_chipsWidth = 0;
    void *m_playerJob = nullptr;   // kills the embedded mpv when the app ends, however it ends   // the library panel's width the chips need, counts left out   // the library's width about to be set, for panelFits   // the window's normal size while full screen hides it   // the picture as it was when a panel's edge was pressed
    QTimer m_surfaceDragEnd;
    bool m_placeWithPage = false;
    void schedulePlace();
    void watchVideoMouse();
    bool videoShowing() const;
    QTimer *m_placeTimer = nullptr;
    QTimer *m_videoMouse = nullptr;
    QPoint m_lastVideoMouse;
    int m_videoMouseStill = 0;
    bool m_surfaceOnPage = false;
    bool m_miniClosed = false;      // the mini player was closed for this video
    std::optional<QPoint> m_miniAt; // where it was dragged to
    int m_miniWidth = 0;            // as it was sized (0: the default)
    QPointer<QWidget> m_beforeVideo; // the page the video page was opened over
    QRect m_videoArea;              // the picture's visible part, global
    bool m_barFits = false;         // its bottom edge is in view, so the controls can show there
    int m_keepAspect = -1;          // what mpv was last told: its window has the video's shape
    // A video list's page shows its videos as YouTube's grid of cards.
    tiles::VideoCardDelegate *m_cardDelegate = nullptr;
    class QStyledItemDelegate *m_rowDelegateSongs = nullptr;
    QWidget *m_columnsBar = nullptr;
    bool m_videoGrid = false;
    void setVideoGrid(bool grid);
    void fitVideoGrid();
    void setVideoFullScreen(bool full);
    LyricsSource m_lyricsSource;
    OfflineStore m_offline;
    class QPushButton *m_downloadButton = nullptr;
    void showDownloadState();
    void toggleDownload(int index);
    QStringList targetsOf(const Playlist &p) const;
    LyricsView *m_lyricsView = nullptr;
    QString m_lyricsKey;           // the song the lyrics page is for
    QWidget *m_beforeLyrics = nullptr;
    QLabel *m_greeting = nullptr;
    QLabel *m_toast = nullptr;
    QTimer m_toastTimer;
    class UpdateChecker *m_updates = nullptr;
    bool m_updateAsked = false;     // the check was asked for, so say "up to date" too
    QPushButton *m_updateButton = nullptr;   // the top bar's, while something is behind
    class QSystemTrayIcon *m_tray = nullptr;
    class TrayMenu *m_trayMenu = nullptr;
    bool m_quitting = false;        // Quit, not the window's close button kept in the tray
    QSet<int> m_failedSongs;        // entries that failed since a song last opened
    bool m_failNoticeShown = false;
    QWidget *m_welcome = nullptr;   // Home's card while the library is empty
    QLabel *m_emptyHint = nullptr;
    QLabel *m_statLists = nullptr;
    QLabel *m_statSongs = nullptr;
    QLabel *m_statHours = nullptr;
    QLabel *m_statNew = nullptr;
    QListWidget *m_recentAdded = nullptr;
    QListWidget *m_recentPlayed = nullptr;
    QLabel *m_recentPlayedTitle = nullptr;
    QWidget *m_playlistCover = nullptr;
    QLabel *m_playlistKind = nullptr;
    QLabel *m_playlistName = nullptr;
    QLabel *m_playlistInfo = nullptr;
    QHBoxLayout *m_headRow = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QElapsedTimer m_splitMoved;      // a panel edge was just dragged
    QLineEdit *m_songSearch = nullptr;
    bool m_renameCancelled = false;
    int m_playingRow = -1;
    QPushButton *m_addedHeader = nullptr;
    QList<std::pair<SortKey, QPushButton *>> m_sortButtons;
    SortKey m_sortKey = SortKey::Custom;
    bool m_sortDescending = false;
    quint32 m_shuffleSeed = 0;     // the order a shuffled list is shown in
    QWidget *m_pageShuffle = nullptr;
    // The file as it was before the last drag or removal, for Ctrl+Z.
    QString m_undoPath;
    QByteArray m_undoBytes;
    // Applied to mpv's playlist once it has loaded the list (see play()).
    // One mpv for the app. While the one just started is not answering yet,
    // a second Play waits for it instead of starting another (which, with
    // no window, would play unseen and outlive the app).
    QElapsedTimer m_launching;
    std::optional<QueuedPlay> m_queuedPlay;
    QList<qint64> m_launchedPids;
    void closeExtraPlayers(bool all);
    QString m_pendingPath;
    QVector<int> m_pendingOrder;
    int m_pendingSerial = 0;
    // The playing list's entries, for the bar's song number.
    QString m_numberedList;
    QStringList m_numberedEntries;
    QWidget *m_homeScroll = nullptr;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanSoon;
    QListWidget *m_songs = nullptr;

    NowPlayingBar *m_nowPlaying = nullptr;
};
