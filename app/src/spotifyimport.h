#pragma once

#include <QDialog>
#include <QNetworkAccessManager>
#include <QPair>
#include <QStringList>
#include <QVector>

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

// Spotify lists brought into the library without a key, as the bot does
// (Takanosu-YT-Indexer vault/spotify.py): the public embed player at
// open.spotify.com/embed/<type>/<id> carries the list in its __NEXT_DATA__.
// A playlist or album link is one page (Spotify gives at most 100 songs);
// a list of track links (Spotify's Ctrl+A, Ctrl+C) is a page each, four at
// a time, so private lists and long ones come too.
namespace spotify {

struct Track {
    QString artist;
    QString title;
};

// What a pasted text is.
struct Request {
    enum Kind { Nothing, Playlist, Album, Tracks } kind = Nothing;
    QString id;            // a playlist's or album's
    QStringList trackIds;  // Tracks, in order, once each
    bool privateLink = false;   // a playlist link with pt= (shared private)
};
Request parse(const QString &text);

class Importer : public QObject {
    Q_OBJECT
public:
    explicit Importer(QObject *parent = nullptr);
    void start(const Request &request);
    void cancel();

signals:
    void progress(int done, int total);
    // `name` empty for a track list (the user names it); `error` set when
    // nothing could be read.
    void finished(const QString &name, const QVector<spotify::Track> &tracks, int unread, const QString &error);

private:
    void fetchList();
    void pump();
    void fetchTrack(int index, int attempt);

    QNetworkAccessManager m_net;
    Request m_request;
    QVector<Track> m_tracks;    // by position; an empty title: not read
    int m_next = 0;
    int m_running = 0;
    int m_done = 0;
    int m_serial = 0;
};

} // namespace spotify

// "Import from Spotify": paste, Import, watch "312 / 500".
class SpotifyImportDialog : public QDialog {
    Q_OBJECT
public:
    explicit SpotifyImportDialog(QWidget *parent = nullptr);
    // Called with the list read and its name (asked for a track list).
    std::function<void(const QString &name, const QVector<spotify::Track> &tracks)> onImported;

private:
    void begin();
    QPlainTextEdit *m_text;
    QLabel *m_status;
    QProgressBar *m_bar;
    QPushButton *m_import;
    spotify::Importer m_importer;
};
