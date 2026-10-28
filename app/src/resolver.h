#pragma once

#include <QDate>
#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>

// Finds a video for a search entry nobody has played yet, only so it can have
// a cover and a length: one "yt-dlp ytsearch1:" per query, four at a time,
// remembered on disk. What plays is still songmatch.lua's choice; this is a
// picture and a number.
class CoverResolver : public QObject {
    Q_OBJECT

public:
    explicit CoverResolver(QObject *parent = nullptr);

    void setYtDlp(const QString &path) { m_ytdlp = path; }

    // The id found earlier, or empty; an empty answer also queues the search.
    QString idFor(const QString &query);

    // A video's length in seconds, or 0 when nobody has told us yet.
    int durationFor(const QString &id) const { return m_durations.value(id); }
    // mpv reports the length of what it plays; that is remembered too.
    void rememberDuration(const QString &id, int seconds);
    // Looks a known video's length up (once a session) when nobody has.
    void requestDuration(const QString &id);
    // The channel a video is from, or empty until it has been looked up
    // (the same search as its length gives it).
    QString channelFor(const QString &id) const { return m_channels.value(id); }
    void rememberChannel(const QString &id, const QString &channel);
    void requestChannel(const QString &id);
    // The day a video was uploaded, invalid until it has been looked up: a
    // full yt-dlp read (the quick search does not carry it), several videos
    // per run, remembered on disk.
    QDate uploadDateFor(const QString &id) const { return m_uploads.value(id); }
    void requestUploadDate(const QString &id);

signals:
    void resolved(const QString &query, const QString &id);

private:
    void startNext();
    void remember(const QString &query, const QString &id, int seconds);

    QString m_ytdlp;
    QString m_file;
    QString m_durationFile;
    QString m_channelFile;
    QHash<QString, QString> m_channels;
    QString m_uploadFile;
    QHash<QString, QDate> m_uploads;
    QSet<QString> m_uploadAsked;
    QStringList m_uploadQueue;
    bool m_uploadRunning = false;
    void startUploads();
    QHash<QString, QString> m_ids;
    QHash<QString, int> m_durations;
    QSet<QString> m_known;     // asked already, found or not
    QSet<QString> m_lengthAsked;
    QQueue<QString> m_queue;
    int m_running = 0;
};
