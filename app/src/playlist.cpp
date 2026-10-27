#include "playlist.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace {

// ghost.lua looks for the marker in the first 256 bytes; the app must agree
// with the player on which lists are songs.
constexpr qint64 kMarkerWindow = 256;

// Enough entries to find one with a cover even when the first few are
// searches nobody has played yet.
constexpr int kPreviewEntries = 12;

bool isEntryLine(const QString &line)
{
    return !line.isEmpty() && !line.startsWith('#');
}

} // namespace

Playlist Playlist::peek(const QString &path)
{
    Playlist p;
    QFileInfo info(path);
    p.path = info.absoluteFilePath();
    p.name = info.completeBaseName();
    p.modified = info.lastModified();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return p;
    p.audio = file.peek(kMarkerWindow).contains("#GHOST_AUDIO");

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (!isEntryLine(line))
            continue;
        if (p.preview.size() < kPreviewEntries)
            p.preview << line;
        ++p.count;
    }
    return p;
}

QVector<PlaylistEntry> Playlist::readEntries() const
{
    QVector<PlaylistEntry> entries;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return entries;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QString pendingTitle;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith("#EXTINF:")) {
            // "#EXTINF:-1, Artist - Track": the title is after the first comma.
            const int comma = line.indexOf(',');
            pendingTitle = comma >= 0 ? line.mid(comma + 1).trimmed() : QString();
        } else if (isEntryLine(line)) {
            QString title = pendingTitle;
            if (title.isEmpty()) {
                // A bare search entry still reads as what it searches for.
                title = line;
                title.remove(QStringLiteral("ytdl://ytsearch1:"));
            }
            entries.append({title, line});
            pendingTitle.clear();
        }
    }
    return entries;
}
