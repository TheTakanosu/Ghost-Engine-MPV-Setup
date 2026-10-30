#include "playlist.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
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
        if (line.startsWith(QStringLiteral("#GHOST_SHARED:"))) {
            p.shared = line.mid(14).trimmed();
            continue;
        }
        if (line.startsWith(QStringLiteral("#PLAYLIST:"))) {
            const QString named = line.mid(10).trimmed();
            if (!named.isEmpty())
                p.name = named;
            continue;
        }
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
    QDateTime pendingAdded;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QStringLiteral("#GHOST_ADDED:"))) {
            pendingAdded = QDateTime::fromString(line.mid(13).trimmed(), Qt::ISODate);
        } else if (line.startsWith("#EXTINF:")) {
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
            entries.append({title, line, pendingAdded});
            pendingTitle.clear();
            pendingAdded = {};
        }
    }
    return entries;
}

bool Playlist::writeName(const QString &path, const QString &name)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = file.readAll();
    file.close();
    const bool crlf = raw.contains("\r\n");
    QStringList lines = QString::fromUtf8(raw).split(QRegularExpression(QStringLiteral("\r?\n")));
    const QString tag = QStringLiteral("#PLAYLIST:") + name;
    bool done = false;
    for (QString &line : lines)
        if (line.trimmed().startsWith(QStringLiteral("#PLAYLIST:"))) { line = tag; done = true; break; }
    if (!done) {
        // After the header lines (#EXTM3U, #GHOST_AUDIO), where players expect
        // list-wide tags; ghost.lua still finds #GHOST_AUDIO in the first 256 bytes.
        int at = 0;
        while (at < lines.size() && (lines[at] == "#EXTM3U" || lines[at] == "#GHOST_AUDIO"))
            ++at;
        lines.insert(at, tag);
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(lines.join(crlf ? "\r\n" : "\n").toUtf8());
    return true;
}

bool Playlist::create(const QString &path, const QString &name)
{
    QFile file(path);
    if (file.exists() || !file.open(QIODevice::WriteOnly))
        return false;
    file.write(QStringLiteral("#EXTM3U\r\n#GHOST_AUDIO\r\n#PLAYLIST:%1\r\n").arg(name).toUtf8());
    return true;
}

bool Playlist::append(const QString &path, const QVector<PlaylistEntry> &entries)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = file.readAll();
    file.close();
    const QByteArray eol = raw.contains("\r\n") || raw.isEmpty() ? "\r\n" : "\n";
    QByteArray out;
    if (!raw.isEmpty() && !raw.endsWith('\n'))
        out += eol;
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (const PlaylistEntry &e : entries) {
        out += "#GHOST_ADDED:" + now.toUtf8() + eol;
        out += "#EXTINF:-1, " + e.title.toUtf8() + eol;
        out += e.target.toUtf8() + eol;
    }
    if (!file.open(QIODevice::Append))
        return false;
    return file.write(out) == out.size();
}

bool Playlist::writeOrder(const QString &path, const QVector<int> &order)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = file.readAll();
    const QDateTime modified = file.fileTime(QFileDevice::FileModificationTime);
    file.close();
    const bool crlf = raw.contains("\r\n");
    QStringList lines = QString::fromUtf8(raw).split(QRegularExpression(QStringLiteral("\r?\n")));

    // List-wide tags at the top; then blocks that each end on an entry line
    // and carry its #EXTINF (and anything else written above it); whatever
    // follows the last entry stays at the end.
    QStringList head;
    int at = 0;
    while (at < lines.size() && (lines[at].trimmed().isEmpty() || lines[at] == "#EXTM3U" ||
                                 lines[at] == "#GHOST_AUDIO" || lines[at].startsWith("#PLAYLIST:")))
        head << lines[at++];
    QVector<QStringList> blocks;
    QStringList pending;
    for (; at < lines.size(); ++at) {
        pending << lines[at];
        if (isEntryLine(lines[at].trimmed())) {
            blocks << pending;
            pending.clear();
        }
    }
    QStringList out = head;
    for (int i : order) {
        if (i < 0 || i >= blocks.size())
            return false;   // the file changed under us; leave it alone
        out << blocks[i];
    }
    out << pending;

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(out.join(crlf ? "\r\n" : "\n").toUtf8());
    file.setFileTime(modified, QFileDevice::FileModificationTime);
    return true;
}
