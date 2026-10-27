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

    // Where imported lists live: Music\Ghost Engine. Always one of folders().
    static QString homeFolder();

    // Moves an .m3u into homeFolder(), the way osu! takes in a skin you
    // double-click, and returns where it went (empty if it could not move).
    QString importFile(const QString &path);
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
