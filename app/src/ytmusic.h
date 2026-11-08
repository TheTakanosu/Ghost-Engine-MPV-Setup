#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVector>

#include <functional>

// YouTube Music's own search, the queue it makes after a song (its radio)
// and the songs of a playlist, through the same keyless web client the
// Now playing panel uses for artists. Measured on 2026-11-05: a search
// answers in ~0.6 s inside the app, where yt-dlp took ~1.7 s and a ~72 MB
// process of its own per search.
//
// It is YouTube's internal interface, not a published one: the answers are
// read by looking for the row objects wherever they sit, so a layout change
// around them does not break it, and a failed call just reports nothing.
namespace ytmusic {

enum class Kind { Song, Video, Playlist, Artist };

struct Item {
    Kind kind = Kind::Song;
    QString id;          // a video id, or for a playlist its id without "VL"
    QString title;
    QString artist;      // songs and videos; a playlist's maker
    QString album;
    QString duration;    // "3:12"
    QString extra;       // "143M plays", "28M views", "50 songs"
    QString thumbnail;   // a picture URL YouTube gave with the row
    QString channelId;   // YouTube videos: the channel's "UC..." id, to follow it
    QString avatar;      // and the channel's picture, a small one
    qint64 published = 0;   // a followed channel's video: when, in seconds since 1970

    // The playlist line it plays as: "#EXTINF:-1, <artist> - <title>" over a
    // watch URL, the way the bot writes the lists.
    QString lineTitle() const;
    QString target() const;
};

// A channel's page: its name and picture, then its videos, newest first.
struct ChannelPage {
    QString name;
    QString avatar;
    QString about;       // "756 subscribers  •  27 videos"
    QVector<Item> videos;
    QString next;        // the next page's token, empty at the end
};

struct PlaylistPage {
    QString id;
    QString title;
    QString subtitle;   // "88 tracks • 6 hours, 34 minutes"
    QString thumbnail;
    QVector<Item> songs;
    bool ok = false;
};

// A playlist id in a pasted YouTube or YouTube Music link ("list=PL..."),
// a video id in one ("v=..." or youtu.be/...), or empty.
QString playlistIdIn(const QString &text);
QString videoIdIn(const QString &text);

// The country searches and the home page are ranked for: the one picked in
// Settings ("search/region"), or Windows' Location (Settings > Time &
// language > Region), not the format locale, which is often en-US.
QString searchRegion();
// Windows' Location as a two-letter code, empty when it is not a country.
QString windowsRegion();

class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);

    // One kind of result per call, so each shelf fills as its answer comes.
    // A newer search makes the answers of an older one arrive as nothing.
    void search(const QString &query, Kind kind, std::function<void(QVector<Item>)> done);
    // The songs YouTube Music plays after this one, the song itself first.
    void radio(const QString &videoId, std::function<void(QVector<Item>)> done);
    // A playlist's songs, every page of them (up to a few thousand).
    void playlist(const QString &id, std::function<void(PlaylistPage)> done);

    // YouTube's own search (www.youtube.com, every video, not only music),
    // videos only, a page of ~20. `next` continues where the last page ended;
    // the answer brings the token for the page after, empty at the end.
    void searchYouTube(const QString &query, const QString &next,
                       std::function<void(QVector<Item>, QString next)> done);

    // What YouTube shows beside a video to watch next (~20), the way its
    // home page is made: the YouTube mode's home is built from these.
    void related(const QString &videoId, std::function<void(QVector<Item>)> done);

    // A channel's latest videos (~15) from its public feed, no sign-in:
    // what the Following page is made of.
    void channelFeed(const QString &channelId, std::function<void(QVector<Item>)> done);
    // The channel's Videos tab, every upload page by page (~30 a page):
    // `next` empty for the first page with the channel's header.
    void channelVideos(const QString &channelId, const QString &next, std::function<void(ChannelPage)> done);

    // The region results are ranked for (Turkish songs first in Turkey).
    void setRegion(const QString &countryCode) { m_region = countryCode; }

private:
    void post(const QString &endpoint, const QJsonObject &body, std::function<void(const QJsonObject &)> done,
              bool youtube = false);
    void morePages(PlaylistPage page, const QString &token, int pages, std::function<void(PlaylistPage)> done);

    QNetworkAccessManager m_net;
    QString m_region;
    int m_searchSerial = 0;
    int m_youtubeSerial = 0;
};

} // namespace ytmusic
