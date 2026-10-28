#pragma once

#include "library.h"
#include "mpvclient.h"
#include "resolver.h"
#include "thumbnails.h"
#include "youtube.h"

#include <QFileSystemWatcher>
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
    enum class SortKey { Custom, Title, Artist, Added, Duration };

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
    void undoSongEdit();
    void applyPendingOrder();
    void showHome();
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

    QVector<QPixmap> coversFor(int index);
    QStringList coverIds(const Playlist &p, int wanted);
    QString idForTarget(const QString &target);
    QString findMpv() const;
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
    QWidget *m_chipRow = nullptr;
    QButtonGroup *m_chips = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_libraryList = nullptr;
    tiles::RowDelegate *m_rowDelegate = nullptr;

    // pages
    QStackedWidget *m_pages = nullptr;
    QLabel *m_greeting = nullptr;
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
    // The file as it was before the last drag or removal, for Ctrl+Z.
    QString m_undoPath;
    QByteArray m_undoBytes;
    // Applied to mpv's playlist once it has loaded the list (see play()).
    QString m_pendingPath;
    QVector<int> m_pendingOrder;
    int m_pendingSerial = 0;
    QWidget *m_homeScroll = nullptr;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanSoon;
    QListWidget *m_songs = nullptr;

    NowPlayingBar *m_nowPlaying = nullptr;
};
