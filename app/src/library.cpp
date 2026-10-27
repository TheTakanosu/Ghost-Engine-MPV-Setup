#include "library.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
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

QString Library::importFile(const QString &path)
{
    const QFileInfo source(path);
    if (!source.exists())
        return {};
    if (QDir::cleanPath(source.absolutePath()).compare(homeFolder(), Qt::CaseInsensitive) == 0)
        return source.absoluteFilePath();   // already in the library

    // A random file name: the list's name goes inside it (#PLAYLIST:), so a
    // name may hold characters Windows refuses in file names, and two lists
    // of the same name never collide.
    static const QString alphabet = QStringLiteral("ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789");
    QString target;
    do {
        QString key;
        for (int i = 0; i < 16; ++i)
            key += alphabet[QRandomGenerator::global()->bounded(alphabet.size())];
        target = homeFolder() + '/' + key + '.' + source.suffix().toLower();
    } while (QFileInfo::exists(target));
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
    QSet<QString> seen;
    for (const QString &folder : std::as_const(m_folders)) {
        const QDir dir(folder);
        const auto files = dir.entryInfoList({"*.m3u", "*.m3u8"}, QDir::Files | QDir::Readable);
        for (const QFileInfo &info : files) {
            const QString key = info.absoluteFilePath().toLower();
            if (seen.contains(key))
                continue;
            seen.insert(key);
            m_playlists.append(Playlist::peek(info.absoluteFilePath()));
        }
    }
    // Newest first: the list just exported from Discord is the one wanted.
    std::sort(m_playlists.begin(), m_playlists.end(),
              [](const Playlist &a, const Playlist &b) { return a.modified > b.modified; });
}

void Library::save() const
{
    QSettings settings;
    settings.setValue("library/folders", m_folders);
}
