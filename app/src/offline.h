#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

// Songs kept on disk to play without the internet, as Spotify's downloads.
// A list is marked for download; its songs are fetched by yt-dlp as the
// audio stream YouTube serves (no ffmpeg, nothing converted), one file per
// song, shared by every list that has it. mpv's offline.lua reads the index
// and plays the file in place of the stream.
class OfflineStore : public QObject {
    Q_OBJECT

public:
    explicit OfflineStore(QObject *parent = nullptr);

    void setYtDlp(const QString &path) { m_ytdlp = path; }
    // The video a playlist entry plays, as a URL or "ytsearch1:..." for yt-dlp.
    std::function<QString(const QString &target)> sourceFor;

    static QString folder();
    bool has(const QString &target) const { return m_files.contains(target); }

    // A list is marked by its file name, so a rename or a move keeps it.
    bool isMarked(const QString &listPath) const;
    void download(const QString &listPath, const QStringList &targets);
    // Unmarks the list and deletes the songs no other marked list still has.
    void remove(const QString &listPath, const QStringList &targets,
                const std::function<QStringList(const QString &listFile)> &targetsOf);
    void removeAll();
    // Songs of marked lists not on disk yet are queued (after a rescan).
    void topUp(const QString &listPath, const QStringList &targets);

    int pending(const QString &listPath) const;   // songs of that list still to fetch
    qint64 bytesUsed() const;
    // Files, not entries: two entries that play the same video share one.
    int songCount() const;

signals:
    void changed(const QString &listPath);

private:
    void enqueue(const QString &listPath, const QString &target);
    void startNext();
    void load();
    void save() const;
    QString format() const;

    struct Job { QString list; QString target; };
    QString m_ytdlp;
    QHash<QString, QString> m_files;   // playlist entry -> file in folder()
    QQueue<Job> m_queue;
    QSet<QString> m_queued;            // targets waiting or running
    QSet<QString> m_failed;            // this session; asked again next time
    int m_running = 0;
    QHash<QString, int> m_runningFor;  // downloads running, by list
};
