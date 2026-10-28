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
    // and the bot never nests its exports. The one exception is the library's
    // own folders, one level inside homeFolder().
    void rescan();
    const QVector<Playlist> &playlists() const { return m_playlists; }

    // Library folders are real folders inside homeFolder(), so Explorer shows
    // the same grouping and nothing else has to remember it.
    QStringList subfolders() const { return m_subfolders; }
    QStringList watchPaths() const;
    bool createSubfolder(const QString &name);
    bool renameSubfolder(const QString &from, const QString &to);
    // Moves its playlists back out to the library, then removes the folder:
    // a folder is only a grouping, deleting it loses no list.
    bool dissolveSubfolder(const QString &name);
    // Into a library folder ("" for none); returns the new path, or empty.
    QString moveToSubfolder(const QString &path, const QString &folder);
    // What Windows refuses in a folder name taken out; empty if nothing is left.
    static QString cleanFolderName(const QString &typed);

private:
    void save() const;

    QStringList m_folders;
    QVector<Playlist> m_playlists;
    QStringList m_subfolders;
};
