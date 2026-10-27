#include "library.h"

#include <QDir>
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
