#pragma once

#include "library.h"
#include "lyrics.h"
#include "offline.h"
#include "mpvclient.h"
#include "resolver.h"
#include "thumbnails.h"
#include "youtube.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPixmap>
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
namespace tiles { class RowDelegate; }

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

private:
    enum Filter { AllLists, Songs, Videos };
    // How the open list's songs are shown. Custom is the file's own order,
    // the only one a drag can change.
    enum class SortKey { Custom, Title, Artist, Added, Duration, Shuffled };

    QWidget *buildTopBar();
    QWidget *buildLibrary();
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
    void toast(const QString &text);
    void undoSongEdit();
    void applyPendingOrder();
    void showHome();
    void showSettings();
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
    void deletePlaylist(int index);
    void reopenAfter(const QString &path);
    void syncSearchWidth();
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
    QString m_lastToggled;         // a double-click is two clicks; see buildLibrary
    QElapsedTimer m_toggleClock;
    QWidget *m_chipRow = nullptr;
    QButtonGroup *m_chips = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_libraryList = nullptr;
    tiles::RowDelegate *m_rowDelegate = nullptr;

    // pages
    QStackedWidget *m_pages = nullptr;
    class SettingsPage *m_settings = nullptr;
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
    struct QueuedPlay { QString path; bool shuffle = false; int startAt = 0; QVector<int> order; };
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
