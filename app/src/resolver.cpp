#include "resolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

namespace {

// Each search is a 2-4 s yt-dlp run; four at once fills a new list in a
// minute or so and still leaves the connection to the song playing.
constexpr int kParallel = 4;

void appendLine(const QString &path, const QString &line)
{
    QFile file(path);
    if (file.open(QIODevice::Append)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << line << '\n';
    }
}

} // namespace

CoverResolver::CoverResolver(QObject *parent) : QObject(parent)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    m_file = dir + QStringLiteral("/cover-ids.tsv");
    m_durationFile = dir + QStringLiteral("/durations.tsv");

    // query <TAB> id [<TAB> seconds]; an empty id is a search that found nothing.
    QFile file(m_file);
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);
        while (!in.atEnd()) {
            const QStringList parts = in.readLine().split('\t');
            if (parts.size() < 2 || parts[0].isEmpty())
                continue;
            m_known.insert(parts[0]);
            const QString id = parts[1].trimmed();
            if (!id.isEmpty())
                m_ids.insert(parts[0], id);
            if (parts.size() > 2 && parts[2].toInt() > 0)
                m_durations.insert(id, parts[2].toInt());
        }
    }
    // id <TAB> seconds, as mpv reported them.
    QFile durations(m_durationFile);
    if (durations.open(QIODevice::ReadOnly)) {
        QTextStream in(&durations);
        while (!in.atEnd()) {
            const QStringList parts = in.readLine().split('\t');
            if (parts.size() == 2 && parts[1].toInt() > 0)
                m_durations.insert(parts[0], parts[1].toInt());
        }
    }
}

QString CoverResolver::idFor(const QString &query)
{
    if (query.isEmpty())
        return {};
    if (m_known.contains(query))
        return m_ids.value(query);
    m_known.insert(query);
    m_queue.enqueue(query);
    startNext();
    return {};
}

void CoverResolver::rememberDuration(const QString &id, int seconds)
{
    if (id.isEmpty() || seconds <= 0 || m_durations.value(id) == seconds)
        return;
    m_durations.insert(id, seconds);
    appendLine(m_durationFile, id + '\t' + QString::number(seconds));
}

void CoverResolver::requestDuration(const QString &id)
{
    if (id.isEmpty() || m_durations.contains(id) || m_lengthAsked.contains(id))
        return;
    m_lengthAsked.insert(id);
    m_queue.enqueue(QStringLiteral("id:") + id);
    startNext();
}

void CoverResolver::startNext()
{
    if (m_ytdlp.isEmpty() || !QFileInfo::exists(m_ytdlp))
        return;
    while (m_running < kParallel && !m_queue.isEmpty()) {
        const QString query = m_queue.dequeue();
        auto *process = new QProcess(this);
        ++m_running;
        connect(process, &QProcess::finished, this, [this, process, query] {
            static const QRegularExpression line(QStringLiteral(R"(^([A-Za-z0-9_-]{11})\s+(\S*)$)"));
            // Several lines for an id search: keep the one that is that video.
            const QString wanted = query.startsWith(QStringLiteral("id:")) ? query.mid(3) : QString();
            QRegularExpressionMatch m;
            for (const QString &out : QString::fromUtf8(process->readAllStandardOutput()).split('\n')) {
                m = line.match(out.trimmed());
                if (m.hasMatch() && (wanted.isEmpty() || m.captured(1) == wanted))
                    break;
                m = QRegularExpressionMatch();
            }
            const QString id = m.hasMatch() ? m.captured(1) : QString();
            const int seconds = m.hasMatch() ? int(m.captured(2).toDouble()) : 0;
            process->deleteLater();
            --m_running;
            if (query.startsWith(QStringLiteral("id:"))) {
                rememberDuration(id, seconds);
                if (seconds > 0)
                    emit resolved(QString(), id);
            } else {
                remember(query, id, seconds);
                if (!id.isEmpty())
                    emit resolved(query, id);
            }
            startNext();
        });
        // A flat search result carries the length too, at no extra cost.
        // A known video's length comes from a flat search for its id: no
        // format selection, so yt-dlp neither extracts the video nor goes
        // looking for ffmpeg (a broken ffmpeg.exe beside it popped up a
        // Windows error box per song).
        if (query.startsWith(QStringLiteral("id:")))
            process->start(m_ytdlp, {"--no-warnings", "--flat-playlist", "--print", "%(id)s %(duration)s",
                                     "ytsearch3:" + query.mid(3)});
        else
            process->start(m_ytdlp, {"--no-warnings", "--flat-playlist", "--print", "%(id)s %(duration)s",
                                     "ytsearch1:" + query});
    }
}

void CoverResolver::remember(const QString &query, const QString &id, int seconds)
{
    if (!id.isEmpty())
        m_ids.insert(query, id);
    if (seconds > 0)
        m_durations.insert(id, seconds);
    // A miss is written too (empty id), so it is not searched on every start.
    appendLine(m_file, QString(query).replace('\t', ' ') + '\t' + id +
                           (seconds > 0 ? '\t' + QString::number(seconds) : QString()));
}
