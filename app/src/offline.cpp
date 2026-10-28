#include "offline.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <memory>

namespace {

constexpr int kAtOnce = 2;   // downloads running together

QString indexPath()
{
    return OfflineStore::folder() + QStringLiteral("/index.tsv");
}

} // namespace

OfflineStore::OfflineStore(QObject *parent) : QObject(parent)
{
    QDir().mkpath(folder());
    load();
}

QString OfflineStore::folder()
{
    // offline.lua looks here too: %LOCALAPPDATA%\TheTakanosu\Ghost Engine\offline
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/offline");
}

void OfflineStore::load()
{
    m_files.clear();
    QFile file(indexPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    while (!in.atEnd()) {
        const QStringList parts = in.readLine().split('\t');
        if (parts.size() == 2 && QFileInfo::exists(folder() + '/' + parts[1]))
            m_files.insert(parts[0], parts[1]);
    }
}

void OfflineStore::save() const
{
    // "playlist entry <tab> file name", one song a line; written whole so
    // mpv never reads half of it.
    QSaveFile file(indexPath());
    if (!file.open(QIODevice::WriteOnly))
        return;
    QByteArray out;
    for (auto it = m_files.cbegin(); it != m_files.cend(); ++it)
        out += it.key().toUtf8() + '\t' + it.value().toUtf8() + '\n';
    file.write(out);
    file.commit();
}

QString OfflineStore::format() const
{
    const QString quality = QSettings().value("offline/quality").toString();
    if (quality == "standard")
        return QStringLiteral("bestaudio[abr<=130]/bestaudio");
    if (quality == "saver")
        return QStringLiteral("bestaudio[abr<=70]/worstaudio/bestaudio");
    return QStringLiteral("bestaudio");
}

bool OfflineStore::isMarked(const QString &listPath) const
{
    return QSettings().value("offline/lists").toStringList().contains(QFileInfo(listPath).fileName());
}

void OfflineStore::download(const QString &listPath, const QStringList &targets)
{
    QSettings settings;
    QStringList marked = settings.value("offline/lists").toStringList();
    const QString key = QFileInfo(listPath).fileName();
    if (!marked.contains(key))
        settings.setValue("offline/lists", marked << key);
    m_failed.clear();   // asked for again: try what failed before too
    topUp(listPath, targets);
    emit changed(listPath);
}

void OfflineStore::topUp(const QString &listPath, const QStringList &targets)
{
    if (!isMarked(listPath))
        return;
    for (const QString &t : targets)
        if (!m_files.contains(t) && !m_queued.contains(t) && !m_failed.contains(t))
            enqueue(listPath, t);
    startNext();
}

void OfflineStore::enqueue(const QString &listPath, const QString &target)
{
    m_queue.enqueue({listPath, target});
    m_queued.insert(target);
}

int OfflineStore::pending(const QString &listPath) const
{
    int n = 0;
    for (const Job &j : m_queue)
        n += j.list == listPath ? 1 : 0;
    return n + m_runningFor.value(listPath);
}

void OfflineStore::startNext()
{
    while (m_running < kAtOnce && !m_queue.isEmpty()) {
        const Job job = m_queue.dequeue();
        const QString source = sourceFor ? sourceFor(job.target) : QString();
        if (source.isEmpty() || m_ytdlp.isEmpty()) {
            m_queued.remove(job.target);
            continue;
        }
        ++m_running;
        ++m_runningFor[job.list];
        auto run = std::make_shared<std::function<void(bool)>>();
        *run = [this, job, source, run](bool withLogin) {
            auto *proc = new QProcess(this);
            // node and cookies.txt sit next to yt-dlp in the mpv folder.
            const QString mpvDir = QFileInfo(m_ytdlp).absolutePath();
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert("PATH", QDir::toNativeSeparators(mpvDir) + ';' + env.value("PATH"));
            proc->setProcessEnvironment(env);
            proc->setWorkingDirectory(mpvDir);
            QStringList args{"--no-playlist", "--no-progress", "--no-warnings", "--fixup", "never",
                             "-f", format(), "-o", folder() + QStringLiteral("/%(id)s.%(ext)s"),
                             "--print", "after_move:filepath"};
            // As cookies.lua does for mpv: the first try without, an entry
            // that failed again with cookies and node.
            if (withLogin) {
                const QString cookies = QDir(mpvDir).filePath("cookies.txt");
                if (QFileInfo::exists(cookies))
                    args << "--cookies" << cookies;
                args << "--js-runtimes" << "node";
            }
            args << "--" << source;
            connect(proc, &QProcess::finished, this, [this, proc, job, run, withLogin](int code) {
                proc->deleteLater();
                const QStringList out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed().split('\n');
                const QString file = out.isEmpty() ? QString() : out.last().trimmed();
                if ((code != 0 || !QFileInfo::exists(file)) && !withLogin) {
                    (*run)(true);
                    return;
                }
                --m_running;
                --m_runningFor[job.list];
                m_queued.remove(job.target);
                if (code == 0 && QFileInfo::exists(file)) {
                    m_files.insert(job.target, QFileInfo(file).fileName());
                    save();
                } else {
                    m_failed.insert(job.target);
                }
                emit changed(job.list);
                startNext();
            });
            proc->start(m_ytdlp, args);
        };
        (*run)(false);
    }
}

void OfflineStore::remove(const QString &listPath, const QStringList &targets,
                          const std::function<QStringList(const QString &listFile)> &targetsOf)
{
    QSettings settings;
    QStringList marked = settings.value("offline/lists").toStringList();
    marked.removeAll(QFileInfo(listPath).fileName());
    settings.setValue("offline/lists", marked);
    // Songs another downloaded list still has stay.
    QSet<QString> kept;
    for (const QString &other : marked)
        for (const QString &t : targetsOf(other))
            kept.insert(t);
    QSet<QString> dropped;
    for (const QString &t : targets)
        if (!kept.contains(t) && m_files.contains(t))
            dropped.insert(m_files.take(t));
    // A file goes once no entry left plays it.
    const QList<QString> still = m_files.values();
    for (const QString &file : dropped)
        if (!still.contains(file))
            QFile::remove(folder() + '/' + file);
    // Waiting downloads of this list go too.
    QQueue<Job> rest;
    for (const Job &j : std::as_const(m_queue)) {
        if (j.list == listPath)
            m_queued.remove(j.target);
        else
            rest.enqueue(j);
    }
    m_queue = rest;
    save();
    emit changed(listPath);
}

void OfflineStore::removeAll()
{
    QSettings().remove("offline/lists");
    m_queue.clear();
    m_queued.clear();
    for (const QString &file : QSet<QString>(m_files.cbegin(), m_files.cend()))
        QFile::remove(folder() + '/' + file);
    m_files.clear();
    save();
    emit changed(QString());
}

int OfflineStore::filesFor(const QStringList &targets) const
{
    QSet<QString> files;
    for (const QString &t : targets)
        if (m_files.contains(t))
            files.insert(m_files.value(t));
    return int(files.size());
}

qint64 OfflineStore::bytesFor(const QStringList &targets) const
{
    QSet<QString> files;
    for (const QString &t : targets)
        if (m_files.contains(t))
            files.insert(m_files.value(t));
    qint64 total = 0;
    for (const QString &file : files)
        total += QFileInfo(folder() + '/' + file).size();
    return total;
}

int OfflineStore::songCount() const
{
    return int(QSet<QString>(m_files.cbegin(), m_files.cend()).size());
}

qint64 OfflineStore::bytesUsed() const
{
    qint64 total = 0;
    for (const QString &file : QSet<QString>(m_files.cbegin(), m_files.cend()))
        total += QFileInfo(folder() + '/' + file).size();
    return total;
}
