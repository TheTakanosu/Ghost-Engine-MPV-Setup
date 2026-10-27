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

} // namespace

CoverResolver::CoverResolver(QObject *parent) : QObject(parent)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    m_file = dir + QStringLiteral("/cover-ids.tsv");

    QFile file(m_file);
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream in(&file);
        in.setEncoding(QStringConverter::Utf8);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const int tab = line.lastIndexOf('\t');
            if (tab <= 0)
                continue;
            const QString query = line.left(tab);
            m_known.insert(query);
            const QString id = line.mid(tab + 1).trimmed();
            if (!id.isEmpty())
                m_ids.insert(query, id);
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

void CoverResolver::startNext()
{
    if (m_ytdlp.isEmpty() || !QFileInfo::exists(m_ytdlp))
        return;
    while (m_running < kParallel && !m_queue.isEmpty()) {
        const QString query = m_queue.dequeue();
        auto *process = new QProcess(this);
        ++m_running;
        connect(process, &QProcess::finished, this, [this, process, query] {
            static const QRegularExpression idLine(QStringLiteral(R"(^[A-Za-z0-9_-]{11}$)"));
            const QString out = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
            const QString id = idLine.match(out).hasMatch() ? out : QString();
            process->deleteLater();
            --m_running;
            remember(query, id);
            if (!id.isEmpty())
                emit resolved(query, id);
            startNext();
        });
        process->start(m_ytdlp, {"--no-warnings", "--flat-playlist", "--print", "id",
                                 "ytsearch1:" + query});
    }
}

void CoverResolver::remember(const QString &query, const QString &id)
{
    if (!id.isEmpty())
        m_ids.insert(query, id);
    // A miss is written too (empty id), so it is not searched on every start.
    QFile file(m_file);
    if (file.open(QIODevice::Append)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out << QString(query).replace('\t', ' ') << '\t' << id << '\n';
    }
}
