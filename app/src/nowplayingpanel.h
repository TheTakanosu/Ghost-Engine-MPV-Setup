#pragma once

#include "lyrics.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QPixmap>
#include <QSet>
#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QGridLayout;
class QVBoxLayout;

// An artist as YouTube Music shows them: the wide photo at the top of their
// page, the monthly audience and, for the better known, a biography.
struct ArtistInfo {
    QString name;
    QString audience;   // "130K monthly audience"
    QString about;
    QString source;     // where the biography is from: "Wikipedia"
    QPixmap photo;
    bool found = false;
};

// Asks YouTube Music's own web API (the one its site uses, no key) for an
// artist's page: found by the artist's name (a song's video is often on a
// label's channel), else the channel the video is on. Each artist is asked
// once per session.
class ArtistLookup : public QObject {
    Q_OBJECT

public:
    explicit ArtistLookup(QObject *parent = nullptr);
    void fetch(const QString &name, const QString &channelId);
    const ArtistInfo *known(const QString &key) const;
    static QString keyFor(const QString &name) { return name.trimmed().toLower(); }

signals:
    void ready(const QString &key);

private:
    void post(const QString &endpoint, const QJsonObject &body, std::function<void(const QJsonObject &)> done);
    void fetchPage(const QString &key, const QString &browseId);

    QNetworkAccessManager m_net;
    QHash<QString, ArtistInfo> m_known;
    QSet<QString> m_asking;
};

// The now playing view at the right, Spotify's: the playing list's name, a
// large cover, a switch between the song and its video, the lyrics as they
// are sung, the artist, the credits and what plays next.
class NowPlayingPanel : public QWidget {
public:
    explicit NowPlayingPanel(QWidget *parent = nullptr);

    void setList(const QString &name);
    void setSong(const QString &title, const QString &artist);
    void setCover(const QPixmap &cover);
    // `available`: a song list, where a song can turn into its video. `video`:
    // the whole list plays as videos; `mini`: this one song, in the cover's place.
    void setVideoState(bool available, bool video, bool busy, bool mini, bool miniBusy);
    // The cover's shape: 1 for a square, the video's while it plays there.
    void setCoverAspect(double aspect);
    QWidget *coverArea() const;
    void setLyrics(const Lyrics &lyrics, bool searching);
    void setPosition(double seconds);
    void setArtist(const ArtistInfo *artist);
    void setCredits(const QList<QPair<QString, QString>> &credits, bool asked);
    void setNext(const QString &title, const QString &artist, const QPixmap &cover);
    // A video list plays (a YouTuber, not a song): the card is the channel's,
    // and the lyrics and credits, a song's, stay hidden.
    void setChannelMode(bool channel);

    std::function<void()> onSwitchVideo;
    std::function<void()> onMiniVideo;
    std::function<void()> onOpenLyrics;
    std::function<void()> onAbout;
    std::function<void()> onPlayNext;
    std::function<void()> onClose;
    QWidget *scrollArea() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void showLyricLines();

    QLabel *m_list;
    class SquareCover *m_cover;
    QPushButton *m_switch;
    QPushButton *m_mini;
    // The video buttons' last state, drawn again in a new theme's colours.
    std::function<void()> m_redrawVideoButtons;
    QLabel *m_title;
    QLabel *m_artist;
    class ClickCard *m_lyricsCard;
    QLabel *m_lyricsNow;
    QLabel *m_lyricsNext;
    QWidget *m_artistCard;
    class ArtistPhoto *m_artistPhoto;
    QLabel *m_artistName;
    QLabel *m_artistAudience;
    QLabel *m_artistAbout;
    QLabel *m_artistSource;
    QWidget *m_creditsCard;
    QVBoxLayout *m_creditRows;
    QPushButton *m_showAll;
    QList<QPair<QString, QString>> m_credits;
    bool m_creditsOpen = false;
    bool m_channelMode = false;   // all of them, not the first three
    void showCredits();
    class ClickCard *m_nextCard;
    QLabel *m_nextCover;
    QLabel *m_nextTitle;
    QLabel *m_nextArtist;
    Lyrics m_lyrics;
    double m_position = 0;
    int m_shownLine = -2;
};
