#pragma once

#include "library.h"
#include "mpvclient.h"
#include "resolver.h"
#include "thumbnails.h"
#include "youtube.h"

#include <QMainWindow>

class NowPlayingBar;
class QLabel;
class QLineEdit;
class QListWidget;
class QSplitter;
class QStackedWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

    // Lists opened with the app (double-clicked .m3u files): moved into the
    // library and shown, the way osu! takes in a skin.
    void importFiles(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum Page { Home, AllLists, Songs, Videos };

    void rescan();
    void refill();
    void showPage(int page);
    void play(const QString &path, bool shuffle = false);
    void playSelected(bool shuffle = false);
    void addFolder();
    void showContextMenu(QListWidget *view, const QPoint &pos);

    QWidget *buildSidebar();
    QWidget *buildHome();
    QWidget *buildBrowse();
    QListWidget *makeGrid(bool wrap);
    void fillGrid(QListWidget *grid, const QVector<int> &indices);

    QVector<QPixmap> coversFor(int index);
    QStringList coverIds(const Playlist &p, int wanted);
    const Playlist *playlistAt(QListWidget *view) const;
    QString findMpv() const;
    void rememberPlayed(const QString &path);

    Library m_library;
    ThumbnailCache m_covers;
    youtube::Matches m_matches;
    CoverResolver m_resolver;
    MpvClient m_mpv;

    QSplitter *m_split = nullptr;
    QListWidget *m_nav = nullptr;
    QStackedWidget *m_pages = nullptr;
    QLabel *m_greeting = nullptr;
    QLabel *m_statLists = nullptr;
    QLabel *m_statSongs = nullptr;
    QLabel *m_statHours = nullptr;
    QLabel *m_statNew = nullptr;
    QListWidget *m_recentAdded = nullptr;
    QListWidget *m_recentPlayed = nullptr;
    QLabel *m_recentPlayedTitle = nullptr;
    QLabel *m_browseTitle = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_grid = nullptr;
    NowPlayingBar *m_nowPlaying = nullptr;
    QString m_selectPath;
};
