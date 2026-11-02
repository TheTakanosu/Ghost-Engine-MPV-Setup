#include "updates.h"

#include "settings.h"
#include "theme.h"

#include <QSettings>
#include <QAbstractButton>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
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

namespace {

// A tick box drawn as the app draws its controls: a rounded square, filled
// with the accent and a white tick when on, faint when it cannot be ticked.
class TickBox : public QAbstractButton {
public:
    TickBox()
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(18, 18);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
        if (isChecked()) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme::accent());
            p.drawRoundedRect(box, 4, 4);
            QPen tick(theme::c().onAccent, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(tick);
            p.drawPolyline(QPolygonF{{box.left() + 3.6, box.center().y() + 0.2},
                                     {box.left() + 6.4, box.bottom() - 3.6},
                                     {box.right() - 3.2, box.top() + 4}});
        } else {
            p.setPen(QPen(isEnabled() ? theme::c().faint : theme::c().track, 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(box, 4, 4);
        }
    }
};

QLabel *text(const QString &html, qreal points, const QColor &colour, int weight = QFont::Normal)
{
    auto *label = new QLabel(html);
    QFont f = label->font();
    f.setPointSizeF(points);
    f.setWeight(QFont::Weight(weight));
    label->setFont(f);
    label->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(colour.name()));
    label->setWordWrap(true);
    return label;
}

} // namespace

UpdateDialog::UpdateDialog(const QList<updates::Part> &parts, QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Updates");
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(430);

    bool anyNewer = false;
    for (const updates::Part &p : parts)
        anyNewer = anyNewer || p.newer;

    auto *title = text("Updates", 13, theme::c().textBright, QFont::DemiBold);
    auto *close = new QPushButton(QChar(0xE711));
    close->setCursor(Qt::PointingHandCursor);
    close->setFixedSize(28, 28);
    close->setToolTip("Close");
    close->setStyleSheet(theme::css(QStringLiteral(
        "QPushButton { font-family: 'Segoe Fluent Icons', 'Segoe MDL2 Assets'; font-size: 10pt; color: %1;"
        " background: transparent; border: none; border-radius: 14px; }"
        "QPushButton:hover { background: @raised; color: %2; }").arg(theme::c().muted.name(), theme::c().textBright.name())));
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    auto *head = new QHBoxLayout;
    head->addWidget(title, 1);
    head->addWidget(close, 0, Qt::AlignTop);
    auto *about = text(anyNewer ? "Pick what to update. Each download is checked against its SHA-256."
                                : "Everything is up to date.", 10, theme::c().textBody);

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(24, 18, 18, 22);
    box->setSpacing(0);
    box->addLayout(head);
    box->addSpacing(2);
    box->addWidget(about);
    box->addSpacing(16);

    // A second line on what each part is for, so a tick is an informed one.
    const auto what = [](const QString &key) -> QString {
        if (key == "ghost") return "The app restarts";
        if (key == "ytdlp") return "Keeps YouTube working · no restart";
        if (key == "mpv") return "Optional · closes the player";
        return "Answers YouTube's JavaScript checks · no restart";
    };
    const QString link = QStringLiteral("color: %1; text-decoration: none;").arg(theme::accentText().name());
    for (const updates::Part &p : parts) {
        auto *tick = new TickBox;
        tick->setChecked(p.newer && (p.key == "ghost" || p.key == "ytdlp"));
        tick->setEnabled(p.newer);
        auto *words = new QVBoxLayout;
        words->setSpacing(2);
        if (p.newer) {
            const QString versions = p.latest.isEmpty() ? p.installed + QStringLiteral(" · too old")
                                                        : p.installed + QStringLiteral(" → ") + p.latest;
            words->addWidget(text(QStringLiteral("<span style='color:%1; font-weight:700'>%2</span>"
                                                 "&nbsp;&nbsp;<span style='color:%3'>%4</span>")
                                      .arg(theme::c().textBright.name(), p.name.toHtmlEscaped(), theme::c().textBody.name(), versions.toHtmlEscaped()),
                                  10.5, theme::c().textBright));
            QString line = what(p.key);
            if (!p.notes.isEmpty())
                line += QStringLiteral(" · <a href='%1' style='%2'>What's new</a>").arg(p.notes, link);
            auto *second = text(line, 9.5, theme::c().muted);
            second->setTextFormat(Qt::RichText);
            second->setOpenExternalLinks(true);
            words->addWidget(second);
        } else {
            const QString state = p.latest.isEmpty() && p.key != "node" ? QStringLiteral("couldn't check")
                                                                        : QStringLiteral("up to date");
            words->addWidget(text(QStringLiteral("%1 %2 · %3").arg(p.name, p.installed, state).toHtmlEscaped(), 10.5, theme::c().muted));
        }
        auto *row = new QHBoxLayout;
        row->setSpacing(12);
        row->addWidget(tick, 0, Qt::AlignTop);
        row->addLayout(words, 1);
        box->addLayout(row);
        box->addSpacing(12);
        m_rows.append({p, tick});
    }

    m_status = text(QString(), 9.5, theme::c().textBody);
    m_status->hide();
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(110);
    theme::style(m_log, QStringLiteral("QPlainTextEdit { background: @window; color: %1; border: 1px solid "
                                        "@line; border-radius: 8px; padding: 6px; font-size: 9pt; }")
                             .arg(theme::c().textBody.name()));
    m_log->hide();
    box->addWidget(m_status);
    box->addWidget(m_log);

    // Later outlined, Update filled with the accent: one strong button.
    m_later = new QPushButton(anyNewer ? "Later" : "Close");
    m_update = new QPushButton;
    const QString shape = QStringLiteral("min-height: 36px; max-height: 36px; padding: 0 20px; border-radius: 9px;"
                                         " font-size: 10.5pt; font-weight: 600;");
    theme::style(m_later, QStringLiteral(
        "QPushButton { %1 color: %2; background: transparent; border: 1px solid @tick; }"
        "QPushButton:hover { background: @raised; }").arg(shape, theme::c().textBright.name()));
    theme::style(m_update, QStringLiteral(
        "QPushButton { %1 color: @onAccent; background: %2; border: none; }"
        "QPushButton:hover { background: @accentHover; }"
        "QPushButton:disabled { background: @track; color: @muted; }").arg(shape, theme::accent().name()));
    for (QPushButton *b : {m_later, m_update})
        b->setCursor(Qt::PointingHandCursor);
    m_update->setVisible(anyNewer);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
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
        connect(tick, &QAbstractButton::toggled, this, count);
    count();
    connect(m_later, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_update, &QPushButton::clicked, this, &UpdateDialog::start);
}

void UpdateDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(theme::c().line, 1));
    p.setBrush(theme::c().popup);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
}

void UpdateDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragFrom = event->globalPosition().toPoint() - frameGeometry().topLeft();
    QDialog::mousePressEvent(event);
}

void UpdateDialog::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && !m_dragFrom.isNull())
        move(event->globalPosition().toPoint() - m_dragFrom);
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

// -- what's new ------------------------------------------------------------------

namespace {

// What each version brings, as its window says it. A version with no lines
// opens without one.
QStringList notesFor(const QString &version)
{
    if (version == QLatin1String("1.3.1"))
        return {
            "Themes: Default, Black, Light, or System to follow Windows. Settings > Appearance.",
            "History on your profile, and your top videos apart from your top songs.",
            "The playlist you play moves to the top of your library.",
            "Pick up where you left off: what played last waits in the player bar.",
            "Fixed: a video list opened after songs played as videos had no picture.",
        };
    return {};
}

} // namespace

void WhatsNewDialog::showIfUpdated(QWidget *parent)
{
    QSettings settings;
    const QString now = QStringLiteral(GHOST_VERSION);
    const QString seen = settings.value("whatsnew/seen").toString();
    // Before 1.3.1 nothing was written here: settings of any other kind
    // mean the app ran before, so this is an update, not an install.
    const bool ranBefore = !seen.isEmpty() || settings.contains("window/geometry");
    settings.setValue("whatsnew/seen", now);
    const QStringList lines = notesFor(now);
    if (seen == now || !ranBefore || lines.isEmpty())
        return;
    WhatsNewDialog dialog(now, lines, parent);
    dialog.exec();
}

WhatsNewDialog::WhatsNewDialog(const QString &version, const QStringList &lines, QWidget *parent) : QDialog(parent)
{
    setWindowTitle("What's new");
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(430);

    auto *title = text(QStringLiteral("Updated to %1").arg(version), 13, theme::c().textBright, QFont::DemiBold);
    auto *about = text("What's new:", 10, theme::c().textBody);
    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(24, 18, 24, 22);
    box->setSpacing(0);
    box->addWidget(title);
    box->addSpacing(4);
    box->addWidget(about);
    box->addSpacing(12);
    for (const QString &line : lines) {
        auto *row = new QHBoxLayout;
        row->setSpacing(10);
        auto *dot = text(QStringLiteral("•"), 10.5, theme::accent(), QFont::Bold);
        dot->setWordWrap(false);
        auto *words = text(line, 10.5, theme::c().textBright);
        words->setTextFormat(Qt::PlainText);
        row->addWidget(dot, 0, Qt::AlignTop);
        row->addWidget(words, 1, Qt::AlignTop);
        box->addLayout(row);
        box->addSpacing(8);
    }
    auto *notes = text(QStringLiteral("<a href='https://github.com/TheTakanosu/Ghost-Engine-MPV-Setup/releases/tag/v%1' "
                                      "style='color:%2; text-decoration:none'>All the changes</a>")
                           .arg(version, theme::accent().name()), 10, theme::c().textBody);
    notes->setOpenExternalLinks(true);
    auto *ok = new QPushButton("Got it");
    ok->setCursor(Qt::PointingHandCursor);
    theme::style(ok, QStringLiteral(
        "QPushButton { min-height: 36px; max-height: 36px; padding: 0 22px; border-radius: 9px; font-size: 10.5pt;"
        " font-weight: 600; color: @onAccent; background: @accent; border: none; }"
        "QPushButton:hover { background: @accentHover; }"));
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(notes, 1, Qt::AlignVCenter);
    buttons->addWidget(ok);
    box->addSpacing(8);
    box->addLayout(buttons);
}

void WhatsNewDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(theme::c().line, 1));
    p.setBrush(theme::c().popup);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
}

void WhatsNewDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragFrom = event->globalPosition().toPoint() - frameGeometry().topLeft();
    QDialog::mousePressEvent(event);
}

void WhatsNewDialog::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && !m_dragFrom.isNull())
        move(event->globalPosition().toPoint() - m_dragFrom);
}
