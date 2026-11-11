#include "library.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

Library::Library()
{
    QSettings settings;
    m_folders = settings.value("library/folders").toStringList();
    if (m_folders.isEmpty())
        m_folders << QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir().mkpath(homeFolder());
    if (!m_folders.contains(homeFolder(), Qt::CaseInsensitive))
        m_folders.prepend(homeFolder());
}

QString Library::homeFolder()
{
    return QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + "/Ghost Engine");
}

// A random file name: the list's name goes inside it (#PLAYLIST:), so a name
// may hold characters Windows refuses in file names, and two lists of the
// same name never collide.
static QString freshPath(const QString &dir, const QString &suffix)
{
    static const QString alphabet = QStringLiteral("ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789");
    QString target;
    do {
        QString key;
        for (int i = 0; i < 16; ++i)
            key += alphabet[QRandomGenerator::global()->bounded(alphabet.size())];
        target = dir + '/' + key + '.' + suffix;
    } while (QFileInfo::exists(target));
    return target;
}

QString Library::createPlaylist(const QString &name, const QString &folder, bool radio)
{
    const QString target = freshPath(folder.isEmpty() ? homeFolder() : homeFolder() + '/' + folder, QStringLiteral("m3u"));
    return Playlist::create(target, name, radio) ? target : QString();
}

QString Library::importFile(const QString &path)
{
    const QFileInfo source(path);
    if (!source.exists())
        return {};
    const QString from = QDir::cleanPath(source.absolutePath());
    if (from.compare(homeFolder(), Qt::CaseInsensitive) == 0 ||
        from.startsWith(homeFolder() + '/', Qt::CaseInsensitive))
        return source.absoluteFilePath();   // already in the library

    const QString target = freshPath(homeFolder(), source.suffix().toLower());
    const QString name = Playlist::peek(path).name;

    // rename moves within a drive; across drives it is a copy and a delete.
    if (!QFile::rename(path, target)) {
        if (!QFile::copy(path, target))
            return {};
        QFile::remove(path);
    }
    Playlist::writeName(target, name);
    return target;
}

void Library::addFolder(const QString &folder)
{
    const QString clean = QDir::cleanPath(folder);
    if (clean.isEmpty() || m_folders.contains(clean, Qt::CaseInsensitive))
        return;
    m_folders << clean;
    save();
}

void Library::removeFolder(const QString &folder)
{
    m_folders.removeAll(folder);
    save();
}

void Library::rescan()
{
    m_playlists.clear();
    m_subfolders.clear();
    for (const QFileInfo &sub : QDir(homeFolder()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase))
        m_subfolders << sub.fileName();
    QSet<QString> seen;
    auto scan = [&](const QString &folder, const QString &group) {
        const QDir dir(folder);
        const auto files = dir.entryInfoList({"*.m3u", "*.m3u8"}, QDir::Files | QDir::Readable);
        for (const QFileInfo &info : files) {
            const QString key = info.absoluteFilePath().toLower();
            if (seen.contains(key))
                continue;
            seen.insert(key);
            m_playlists.append(Playlist::peek(info.absoluteFilePath()));
            m_playlists.last().folder = group;
        }
    };
    for (const QString &folder : std::as_const(m_folders))
        scan(folder, QString());
    for (const QString &sub : std::as_const(m_subfolders))
        scan(homeFolder() + '/' + sub, sub);
    // Newest first: the list just exported from Discord is the one wanted.
    std::sort(m_playlists.begin(), m_playlists.end(),
              [](const Playlist &a, const Playlist &b) { return a.modified > b.modified; });
}

QStringList Library::watchPaths() const
{
    QStringList paths = m_folders;
    for (const QString &sub : m_subfolders)
        paths << homeFolder() + '/' + sub;
    return paths;
}

QString Library::cleanFolderName(const QString &typed)
{
    static const QRegularExpression forbidden(QStringLiteral(R"([\\/:*?"<>|]+)"));
    QString name = typed;
    name.replace(forbidden, QStringLiteral(" "));
    name = name.simplified();
    while (name.endsWith('.'))   // Windows drops trailing dots
        name.chop(1);
    return name.trimmed();
}

bool Library::createSubfolder(const QString &name)
{
    const QString clean = cleanFolderName(name);
    return !clean.isEmpty() && QDir(homeFolder()).mkdir(clean);
}

bool Library::renameSubfolder(const QString &from, const QString &to)
{
    const QString clean = cleanFolderName(to);
    return !clean.isEmpty() && QDir(homeFolder()).rename(from, clean);
}

bool Library::dissolveSubfolder(const QString &name)
{
    const QDir dir(homeFolder() + '/' + name);
    for (const QFileInfo &info : dir.entryInfoList({"*.m3u", "*.m3u8"}, QDir::Files))
        if (moveToSubfolder(info.absoluteFilePath(), QString()).isEmpty())
            return false;
    // Only an empty folder goes; anything else the user put there stays.
    return QDir(homeFolder()).rmdir(name);
}

QString Library::moveToSubfolder(const QString &path, const QString &folder)
{
    const QFileInfo source(path);
    const QString dir = folder.isEmpty() ? homeFolder() : homeFolder() + '/' + folder;
    QString target = dir + '/' + source.fileName();
    if (QDir::cleanPath(source.absoluteFilePath()).compare(QDir::cleanPath(target), Qt::CaseInsensitive) == 0)
        return source.absoluteFilePath();
    // The file names are random keys, so a clash is someone else's file of
    // the same name: keep both.
    for (int n = 2; QFileInfo::exists(target); ++n)
        target = dir + '/' + source.completeBaseName() + QStringLiteral("-%1.").arg(n) + source.suffix();
    // Moving keeps the file's date, so "Recently added" does not change.
    return QFile::rename(path, target) ? target : QString();
}

void Library::save() const
{
    QSettings settings;
    settings.setValue("library/folders", m_folders);
}
