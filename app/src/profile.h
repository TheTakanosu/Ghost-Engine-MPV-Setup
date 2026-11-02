#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QListWidget;
class QVBoxLayout;
class QHBoxLayout;

// Who is listening, as Discord knows them. Discord's local pipe answers a
// presence app's handshake with the signed-in user (name, avatar), no login
// needed; ghost_rpc.py holds its own connection, this one only says hello
// and leaves. The last answer is kept, for when Discord is closed.
class DiscordProfile : public QObject {
    Q_OBJECT

public:
    explicit DiscordProfile(QObject *parent = nullptr);
    void refresh();

    bool known() const { return !m_id.isEmpty(); }
    QString name() const { return m_name; }       // the display name
    QString handle() const { return m_handle; }   // the @username
    QPixmap avatar() const { return m_avatar; }
    QString id() const { return m_id; }
    QString avatarUrl() const { return m_avatarUrl; }   // Discord's address for it

signals:
    void changed();

private:
    void tryPipe();
    void read();
    void fetchAvatar(const QString &hash);
    static QString avatarPath();

    QLocalSocket m_pipe;
    QNetworkAccessManager m_net;
    QByteArray m_buffer;
    int m_pipeNumber = 0;
    QString m_id, m_name, m_handle, m_avatarUrl;
    QPixmap m_avatar;
};

// Pictures by their web address, each fetched once per session: the avatar
// of whoever shared a list.
class RemotePictures : public QObject {
    Q_OBJECT

public:
    explicit RemotePictures(QObject *parent = nullptr) : QObject(parent) {}
    QPixmap get(const QString &url);

signals:
    void ready(const QString &url);

private:
    QNetworkAccessManager m_net;
    QHash<QString, QPixmap> m_pictures;
};

// What has been listened to: a song counts as played after 30 seconds of it,
// as Spotify counts. Kept in one small file next to the app's other data.
// Songs and videos are counted apart: what played on the video page is a
// video, a song in the mini video is still a song. The history is the last
// plays in order, both kinds, kept short (kHistoryMax, kHistoryDays); it is
// text only, the covers come from the cover cache.
class ListeningStats {
public:
    struct Song { QString key, title, artist, id; int plays = 0; };
    struct Count { QString name; int plays = 0; };
    struct Entry { QDateTime at; QString title, artist, id, list; bool video = false; };
    static constexpr int kHistoryMax = 1000;
    static constexpr int kHistoryDays = 90;

    ListeningStats();
    void played(const QString &title, const QString &artist, const QString &id, const QString &list, bool video);

    // Settings > Privacy: off stops the history and deletes it.
    static bool keepsHistory();
    void setKeepHistory(bool on);
    void clearHistory();
    QList<Entry> history() const;

    // Plays counted as songs before videos were counted apart, moved over
    // once: those whose video is in a video list.
    void sortOutVideos(const QSet<QString> &videoIds);

    QList<Song> topSongs(int count) const;
    QList<Song> topVideos(int count) const;
    QList<Count> topArtists(int count) const;
    QList<Count> topLists(int count) const;   // names are the lists' paths
    QDateTime since() const;
    bool empty() const;

private:
    void save() const;
    QJsonObject m_data;
};

// The profile page: the Discord card at the top, what is playing now, as
// Discord shows it to friends, then the artists, songs and lists played most.
class ProfilePage : public QWidget {
public:
    explicit ProfilePage(QWidget *parent = nullptr);

    void setUser(const QString &name, const QString &handle, const QPixmap &avatar, const QString &counts);
    void setListening(const QPixmap &cover, const QString &title, const QString &artist,
                      const QString &list, bool paused);
    void setPosition(double seconds, double duration);
    void setIdle();
    struct Artist { QString name; QPixmap photo; int plays = 0; };
    struct Song { QString title, artist; QPixmap cover; int plays = 0; };
    void setTopArtists(const QList<Artist> &artists);
    void setTopSongs(const QList<Song> &songs);
    void setTopVideos(const QList<Song> &videos);
    struct Played { QString title, subtitle, when; QPixmap cover; bool video = false; };
    void setHistory(const QList<Played> &plays, bool more, bool keeping);
    std::function<void()> onMoreHistory;
    void setListsShelf(QListWidget *shelf);
    void setListsVisible(bool visible);
    void setSince(const QDateTime &since, bool empty);

    std::function<void()> onOpenPlaying;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void fillRanked(QVBoxLayout *into, const QList<Song> &songs);

    QLabel *m_avatar;
    QLabel *m_name;
    QLabel *m_info;
    QWidget *m_nowCard;
    QLabel *m_nowCover;
    QLabel *m_nowKicker;
    QLabel *m_nowTitle;
    QLabel *m_nowArtist;
    QLabel *m_nowFrom;
    QString m_nowList;
    bool m_nowPaused = false;
    QLabel *m_artistsTitle;
    QHBoxLayout *m_artists;
    QLabel *m_songsTitle;
    QVBoxLayout *m_songs;
    QLabel *m_videosTitle;
    QVBoxLayout *m_videos;
    QLabel *m_historyTitle;
    QVBoxLayout *m_history;
    QLabel *m_historyNote;
    QPushButton *m_historyMore;
    QLabel *m_listsTitle;
    QVBoxLayout *m_listsBox;
    QLabel *m_since;
};
