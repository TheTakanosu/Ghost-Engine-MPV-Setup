#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

// One line a player plays: the bot writes "#EXTINF:-1, <artist> - <track>"
// above the URL or "ytdl://ytsearch1:<query>" it stands for.
struct PlaylistEntry {
    QString title;
    QString target;
};

// An .m3u file as the library shows it. The header is only read when listing
// a folder; entries are read on demand, since a library can hold hundreds of
// lists and nobody looks inside most of them.
class Playlist {
public:
    static Playlist peek(const QString &path);

    QString path;
    QString name;         // file name without .m3u
    bool audio = false;   // "#GHOST_AUDIO" near the top: the bot's song lists
    int count = 0;        // entries, counted while peeking
    QDateTime modified;

    QVector<PlaylistEntry> readEntries() const;
};
