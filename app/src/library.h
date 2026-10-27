#pragma once

#include "playlist.h"

#include <QStringList>
#include <QVector>

// The folders the app watches for .m3u files, and what is in them. The bot's
// exports land in Downloads, so that is where it looks unless told otherwise.
class Library {
public:
    Library();

    QStringList folders() const { return m_folders; }
    void addFolder(const QString &folder);
    void removeFolder(const QString &folder);

    // Reads every folder again. Not recursive: a Downloads folder can be deep,
    // and the bot never nests its exports.
    void rescan();
    const QVector<Playlist> &playlists() const { return m_playlists; }

private:
    void save() const;

    QStringList m_folders;
    QVector<Playlist> m_playlists;
};
