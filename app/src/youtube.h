#pragma once

#include <QHash>
#include <QString>

namespace youtube {

// The 11-character video id in a playlist line, or an empty string. Search
// entries ("ytdl://ytsearch1:...") have none of their own.
QString videoId(const QString &target);

// The query of a search entry, or an empty string.
QString searchQuery(const QString &target);

// The songs songmatch.lua has already found: query -> video id, read from the
// ghost-matches.tsv it appends to in mpv's config folder. Read again when the
// file has grown, so songs played since show their covers.
class Matches {
public:
    QString idFor(const QString &query);

private:
    void reloadIfChanged();

    QHash<QString, QString> m_ids;
    qint64 m_size = -1;
};

} // namespace youtube
