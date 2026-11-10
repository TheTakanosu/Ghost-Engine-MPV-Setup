#pragma once

#include "ytmusic.h"

#include <QSet>
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
namespace tiles { class VideoCardDelegate; }

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

    // YouTube mode's home, shown when it is opened with nothing typed: a grid
    // as YouTube's home has, made of what YouTube shows beside the videos
    // watched here last, newest first, and it goes on as it is scrolled.
    // Chips at the top: All, then the channels watched most.
    void showYouTubeHome();
    // These videos in the home's grid under a title (the History), until a
    // chip or the home is picked again.
    void showVideos(const QString &title, const QVector<ytmusic::Item> &items);
    // A channel's own videos, all of them page by page, under its name.
    void showChannel(const QString &channelId, const QString &name);
    std::function<void(const QString &channelId, const QString &name)> onChannelShown;
    // The channel page's Follow: asked and told through the app.
    std::function<bool(const QString &channelId)> isFollowed;
    std::function<void(const QString &channelId, const QString &name, bool follow)> onFollow;
    void refreshFollow();
    bool youTubeMode() const { return m_youtube; }
    void setYouTubeMode(bool youtube);
    // The videos watched here, newest first, and the channels watched most.
    std::function<QStringList()> watchedVideos;
    std::function<QStringList()> watchedChannels;

    // A song's radio, then onPlay; the song alone when there is none.
    void playRadio(const ytmusic::Item &song);

    // What the page shows now, as a key and a way to show it again: the
    // app's Back and Forward. onViewChanged after each new view.
    QString viewKey() const { return m_viewKey; }
    std::function<void()> viewRestore() const { return m_viewGo; }
    std::function<void()> onViewChanged;

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
    void moreYouTube();
    void fitHomeGrid();
    void fillHome(bool reset);
    void homeMore();
    void homeChip(const QString &channel);
    int appendHome(const QVector<ytmusic::Item> &items);
    void searchInGrid(const QString &query);
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

    // YouTube home.
    QWidget *m_homeChips = nullptr;
    QListWidget *m_homeGrid = nullptr;
    QLabel *m_homeStatus = nullptr;
    tiles::VideoCardDelegate *m_homeCards = nullptr;
    QVector<ytmusic::Item> m_home;
    QStringList m_homeSeeds;      // videos whose side lists are still to ask for
    QSet<QString> m_homeSeen;
    class RemotePictures *m_avatars = nullptr;
    QHash<QString, QString> m_avatarOf;   // channel id -> its picture URL, for rows without one (feeds)
    QSet<QString> m_homeSeeded;   // videos already asked beside
    QString m_homeChannel;        // the chip picked; empty: All
    QString m_homeNext;           // a chip's search, its next page
    bool m_homeLoading = false;
    bool m_homeFixed = false;     // showVideos: nothing more is fetched
    bool m_homeSearch = false;    // the grid shows a search's results, not the home
    QString m_homeChannelId;      // the grid shows this channel's page
    QString m_homeChannelName;
    int m_homeSerial = 0;

    // The playlist view.
    QLabel *m_listTitle = nullptr;
    QLabel *m_listInfo = nullptr;
    QListWidget *m_listSongs = nullptr;
    QPushButton *m_saveButton = nullptr;
    ytmusic::PlaylistPage m_list;
    bool m_listFromLink = false;

    void noteView(const QString &key, std::function<void()> go);
    void showChannelHeader(bool shown);
    class ChannelHeader *m_channelHeader = nullptr;
    QString m_viewKey;
    std::function<void()> m_viewGo;
};
