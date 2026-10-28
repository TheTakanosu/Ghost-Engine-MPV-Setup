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
    QDateTime added;   // "#GHOST_ADDED:" above it; songs added in the app have one
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
    QStringList preview;  // the first few entries, where a cover is looked for
    QDateTime modified;
    QString folder;       // the library folder it sits in (Music\Ghost Engine\<folder>), or empty

    QVector<PlaylistEntry> readEntries() const;

    // The name lives inside the file as "#PLAYLIST:<name>" (the extended M3U
    // tag for it), so it can hold any character; the file name is only a key.
    static bool writeName(const QString &path, const QString &name);

    // Rewrites the entries in `order` (indices into readEntries()), each with
    // the lines above it; an index left out removes that entry. The header,
    // line endings and the file's date stay, so "Date added" does not jump.
    static bool writeOrder(const QString &path, const QVector<int> &order);

    // A new, empty song list, and songs added to the end of one. Each added
    // song carries the moment it was added, so "Date added" means that song
    // and not the whole file; players skip the line as a comment.
    static bool create(const QString &path, const QString &name);
    static bool append(const QString &path, const QVector<PlaylistEntry> &entries);
};
