#include "youtube.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace youtube {

namespace {

QString matchesPath()
{
    // songmatch.lua writes to "~~/", mpv's config folder: %APPDATA%\mpv.
    return qEnvironmentVariable("APPDATA") + QStringLiteral("/mpv/ghost-matches.tsv");
}

} // namespace

QString videoId(const QString &target)
{
    static const QRegularExpression re(
        QStringLiteral(R"((?:[?&]v=|youtu\.be/|/shorts/|^ytdl://)([A-Za-z0-9_-]{11})(?![A-Za-z0-9_-]))"));
    const auto m = re.match(target);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString searchQuery(const QString &target)
{
    static const QRegularExpression re(QStringLiteral(R"(^ytdl://ytsearch\d*:(.+)$)"));
    const auto m = re.match(target);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

QString Matches::idFor(const QString &query)
{
    reloadIfChanged();
    return m_ids.value(query);
}

void Matches::reloadIfChanged()
{
    const QFileInfo info(matchesPath());
    const qint64 size = info.exists() ? info.size() : 0;
    if (size == m_size)
        return;
    m_size = size;
    m_ids.clear();

    QFile file(info.filePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const int tab = line.lastIndexOf('\t');
        if (tab > 0)
            m_ids.insert(line.left(tab), line.mid(tab + 1).trimmed());
    }
}

} // namespace youtube
