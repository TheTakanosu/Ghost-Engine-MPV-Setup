#include "updates.h"

#include "settings.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace updates {

QString script()
{
    const QString path = QFileInfo(mpvconf::path()).absoluteDir().filePath(QStringLiteral("update.ps1"));
    return QFileInfo::exists(path) ? path : QString();
}

namespace {

QStringList powershell(const QStringList &args)
{
    return QStringList{"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", QDir::toNativeSeparators(script())} + args;
}

} // namespace

} // namespace updates

// -- checker -------------------------------------------------------------------

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent) {}

int UpdateChecker::newerCount() const
{
    int n = 0;
    for (const updates::Part &p : m_parts)
        n += p.newer;
    return n;
}

void UpdateChecker::check()
{
    if (m_process)
        return;
    if (updates::script().isEmpty()) {
        emit finished(false);
        return;
    }
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::finished, this, [this] {
        // The JSON is the last line; anything before it is the script saying
        // it could not reach GitHub.
        const QStringList lines = QString::fromUtf8(m_process->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
        m_process->deleteLater();
        m_process = nullptr;
        const QJsonObject report = QJsonDocument::fromJson(lines.isEmpty() ? QByteArray() : lines.last().trimmed().toUtf8()).object();
        if (report.isEmpty()) {
            emit finished(false);
            return;
        }
        m_parts.clear();
        const std::pair<const char *, const char *> known[] = {
            {"ghost", "Ghost Engine"}, {"ytdlp", "yt-dlp"}, {"mpv", "mpv"}, {"node", "node"},
        };
        for (const auto &[key, name] : known) {
            const QJsonObject o = report.value(QLatin1String(key)).toObject();
            if (o.isEmpty())
                continue;   // not installed here (node, an mpv kept elsewhere)
            m_parts.append({QString::fromLatin1(key), QString::fromLatin1(name), o.value("installed").toString(),
                            o.value("latest").toString(), o.value("newer").toBool(), o.value("notes").toString()});
        }
        emit finished(true);
    });
    m_process->start("powershell.exe", updates::powershell({"-Check"}));
}

// -- dialog --------------------------------------------------------------------

UpdateDialog::UpdateDialog(const QList<updates::Part> &parts, QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Updates");
    setMinimumWidth(460);
    auto *title = new QLabel("Updates");
    title->setObjectName("welcomeTitle");
    auto *about = new QLabel("Pick what to update. Each download is checked against its SHA-256, "
                             "and nothing is replaced when one does not match.");
    about->setObjectName("statLabel");
    about->setWordWrap(true);

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(22, 18, 22, 18);
    box->setSpacing(10);
    box->addWidget(title);
    box->addWidget(about);
    box->addSpacing(4);

    // What each part is for, so ticking one is an informed choice.
    const auto what = [](const QString &key) -> QString {
        if (key == "ghost") return "The app and its mpv scripts. Ghost Engine closes and opens again.";
        if (key == "ytdlp") return "What turns YouTube links into music. It breaks when YouTube changes.";
        if (key == "mpv") return "The player. It stops while the update runs.";
        return "Lets yt-dlp answer YouTube's JavaScript checks.";
    };
    for (const updates::Part &p : parts) {
        auto *tick = new QCheckBox;
        tick->setChecked(p.newer && (p.key == "ghost" || p.key == "ytdlp"));
        tick->setEnabled(p.newer);
        QString versions;
        if (p.newer)
            versions = p.latest.isEmpty() ? p.installed + QStringLiteral("  ·  too old")
                                          : p.installed + QStringLiteral("  →  ") + p.latest;
        else if (p.latest.isEmpty() && p.key != "node")
            versions = p.installed + QStringLiteral("  ·  couldn't check");
        else
            versions = p.installed + QStringLiteral("  ·  up to date");
        auto *name = new QLabel(QStringLiteral("<b>%1</b>&nbsp;&nbsp;%2").arg(p.name, versions.toHtmlEscaped()));
        name->setObjectName(p.newer ? "settingName" : "statLabel");
        auto *line = new QLabel(what(p.key));
        line->setObjectName("statLabel");
        line->setWordWrap(true);
        auto *words = new QVBoxLayout;
        words->setSpacing(1);
        words->addWidget(name);
        if (p.newer)
            words->addWidget(line);
        auto *row = new QHBoxLayout;
        row->setSpacing(10);
        row->addWidget(tick, 0, Qt::AlignTop);
        row->addLayout(words, 1);
        if (p.newer && !p.notes.isEmpty()) {
            auto *notes = new QPushButton("What's new");
            notes->setObjectName("link");
            notes->setCursor(Qt::PointingHandCursor);
            const QString url = p.notes;
            connect(notes, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl(url)); });
            row->addWidget(notes, 0, Qt::AlignTop);
        }
        box->addLayout(row);
        m_rows.append({p, tick});
    }

    m_status = new QLabel;
    m_status->setObjectName("statLabel");
    m_status->setWordWrap(true);
    m_status->hide();
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(120);
    m_log->hide();
    box->addWidget(m_status);
    box->addWidget(m_log);

    m_later = new QPushButton("Later");
    m_later->setObjectName("videoAction");
    m_update = new QPushButton;
    m_update->setObjectName("primary");
    for (QPushButton *b : {m_later, m_update})
        b->setCursor(Qt::PointingHandCursor);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_later);
    buttons->addWidget(m_update);
    box->addSpacing(6);
    box->addLayout(buttons);

    const auto count = [this] {
        int n = 0;
        for (const auto &[part, tick] : m_rows)
            n += tick->isChecked();
        m_update->setText(n ? QStringLiteral("Update %1").arg(n) : QStringLiteral("Update"));
        m_update->setEnabled(n > 0);
    };
    for (const auto &[part, tick] : m_rows)
        connect(tick, &QCheckBox::toggled, this, count);
    count();
    connect(m_later, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_update, &QPushButton::clicked, this, &UpdateDialog::start);
}

void UpdateDialog::start()
{
    QStringList keys;
    for (const auto &[part, tick] : m_rows)
        if (tick->isChecked())
            keys << part.key;
    if (keys.isEmpty())
        return;
    if (keys.contains("mpv") && onStopPlayer)
        onStopPlayer();

    // The app replaces itself: it cannot stay open for that, so the script
    // runs in a window of its own, then opens the app (the installed one).
    if (keys.contains("ghost")) {
        const QString installed = QFileInfo(updates::script()).absoluteDir().filePath("GhostEngine.exe");
        const QString reopen = QFileInfo::exists(installed) ? installed : QCoreApplication::applicationFilePath();
        const QStringList args = updates::powershell({"-Only", keys.join(','), "-Unattended",
                                                      "-Reopen", QDir::toNativeSeparators(reopen)});
        if (QProcess::startDetached("powershell.exe", args)) {
            accept();
            if (onQuitForUpdate)
                onQuitForUpdate();
        } else {
            m_status->setText("Could not start update.ps1. Run update.bat in the mpv folder instead.");
            m_status->show();
        }
        return;
    }

    // The rest updates in place, with the script's lines shown as they come.
    for (const auto &[part, tick] : m_rows)
        tick->setEnabled(false);
    m_update->setEnabled(false);
    m_update->setText("Updating...");
    m_later->setEnabled(false);
    m_log->show();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this] {
        for (const QString &line : QString::fromUtf8(m_process->readAll()).split('\n', Qt::SkipEmptyParts))
            if (!line.trimmed().isEmpty())
                m_log->appendPlainText(line.trimmed());
    });
    connect(m_process, &QProcess::finished, this, [this](int code) {
        m_status->setText(code == 0 ? "Done. A list that is playing picks the new yt-dlp up from its next song."
                                    : "update.ps1 stopped with an error; the lines above say why.");
        m_status->show();
        m_update->hide();
        m_later->setText("Close");
        m_later->setEnabled(true);
    });
    m_process->start("powershell.exe", updates::powershell({"-Only", keys.join(','), "-Unattended"}));
}
