#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>

// Finds a video for a search entry nobody has played yet, only so it can have
// a cover: one "yt-dlp ytsearch1:" per query, two at a time, remembered on
// disk. What plays is still songmatch.lua's choice; this is a picture.
class CoverResolver : public QObject {
    Q_OBJECT

public:
    explicit CoverResolver(QObject *parent = nullptr);

    void setYtDlp(const QString &path) { m_ytdlp = path; }

    // The id found earlier, or empty; an empty answer also queues the search.
    QString idFor(const QString &query);

signals:
    void resolved(const QString &query, const QString &id);

private:
    void startNext();
    void remember(const QString &query, const QString &id);

    QString m_ytdlp;
    QString m_file;
    QHash<QString, QString> m_ids;
    QSet<QString> m_known;     // asked already, found or not
    QQueue<QString> m_queue;
    int m_running = 0;
};
