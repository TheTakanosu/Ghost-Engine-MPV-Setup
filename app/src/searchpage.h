#pragma once

#include "ytmusic.h"

#include <QWidget>

#include <functional>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class ThumbnailCache;
namespace icons { class IconButton; }

// What the top search box finds, as Spotify's search page shows it: lists in
// the library whose name matches, then songs, videos and playlists from
// YouTube Music. A song plays with YouTube Music's radio after it; a
// playlist opens to look at first and is saved to the library from there.
// A pasted YouTube or YouTube Music link opens its playlist, or its song.
// In YouTube mode (the switch at the top) it searches YouTube itself: every
// video, not only music, played on the video page.
class SearchPage : public QWidget {
    Q_OBJECT
public:
    struct LibraryMatch {
        QString name;
        QString path;
    };

    SearchPage(ThumbnailCache &covers, QWidget *parent = nullptr);

    // Searches again unless it is what is already shown.
    void setQuery(const QString &text);
    QString query() const { return m_query; }
    void setRegion(const QString &countryCode) { m_client.setRegion(countryCode); }

    // The two scroll areas, for the app's smooth wheel scrolling.
    QList<QWidget *> scrollAreas() const;

    std::function<QVector<LibraryMatch>(const QString &)> libraryMatches;
    // Plays these in a list of their own; `audio` false for videos.
    std::function<void(const QVector<ytmusic::Item> &, const QString &name, int start, bool shuffle, bool audio)> onPlay;
    std::function<void(const QString &name, const QVector<ytmusic::Item> &)> onSave;
    std::function<void(const QString &path)> onOpenLibrary;
    // The right-click menu for these songs (Play, Add to playlist...).
    std::function<void(const QVector<ytmusic::Item> &, const QPoint &global)> onMenu;
    std::function<void(const QString &)> onToast;

    // A song's radio, then onPlay; the song alone when there is none.
    void playRadio(const ytmusic::Item &song);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QListWidget *makeList(bool scrolls);
    void fill(QListWidget *list, const QVector<ytmusic::Item> &items, int shown);
    void fitHeight(QListWidget *list);
    void showResults();
    void openPlaylist(const QString &id, const QString &title);
    void refreshCovers(const QString &id);
    void setStatus(const QString &text);
    void updateEmpty();
    void setYouTubeMode(bool youtube);
    void moreYouTube();
    QVector<ytmusic::Item> picked(QListWidget *list, const QVector<ytmusic::Item> &items) const;

    ThumbnailCache &m_covers;
    ytmusic::Client m_client;
    QString m_query;
    int m_serial = 0;

    QStackedWidget *m_views = nullptr;
    QScrollArea *m_resultsScroll = nullptr;
    QLabel *m_status = nullptr;
    QWidget *m_librarySection = nullptr;
    QWidget *m_libraryChips = nullptr;
    QWidget *m_songsSection = nullptr;
    QWidget *m_videosSection = nullptr;
    QWidget *m_playlistsSection = nullptr;
    QListWidget *m_songsList = nullptr;
    QListWidget *m_videosList = nullptr;
    QListWidget *m_playlistsList = nullptr;
    QPushButton *m_songsMore = nullptr;
    QPushButton *m_videosMore = nullptr;
    QVector<ytmusic::Item> m_songs, m_videos, m_playlists;
    int m_pending = 0;

    // YouTube mode.
    bool m_youtube = false;
    QPushButton *m_musicMode = nullptr;
    QPushButton *m_youtubeMode = nullptr;
    QWidget *m_youtubeSection = nullptr;
    QListWidget *m_youtubeList = nullptr;
    QPushButton *m_youtubeMore = nullptr;
    QVector<ytmusic::Item> m_ytVideos;
    QString m_ytNext;

    // The playlist view.
    QLabel *m_listTitle = nullptr;
    QLabel *m_listInfo = nullptr;
    QListWidget *m_listSongs = nullptr;
    QPushButton *m_saveButton = nullptr;
    ytmusic::PlaylistPage m_list;
    bool m_listFromLink = false;
};
