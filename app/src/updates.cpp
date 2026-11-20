#include "updates.h"

#include "settings.h"
#include "theme.h"

#include <QVersionNumber>
#include <QSettings>
#include <QAbstractButton>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
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

// The update to a version that did not land (1.6, update safety): what is
// installed still runs, and the way on is shown without the script, which
// may be what broke: the release's page and its setup, which installs over
// this one and keeps the playlists, settings and mpv.conf.
class UnfinishedDialog : public QDialog {
public:
    UnfinishedDialog(const QString &now, const QString &wanted, const QString &page, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle("Update");
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setFixedWidth(430);
        const QString link = QStringLiteral("color: %1; text-decoration: none;").arg(theme::accentText().name());
        const QString setup = QStringLiteral("https://github.com/TheTakanosu/Ghost-Engine-MPV-Setup/releases/download/v%1/"
                                             "Ghost-Engine-MPV-Setup.zip").arg(wanted);
        auto *title = text(QStringLiteral("The update to %1 didn't finish").arg(wanted.toHtmlEscaped()), 13,
                           theme::c().textBright, QFont::DemiBold);
        auto *about = text(QStringLiteral("Ghost Engine is still %1 and works as before. Try again from Updates; "
                                          "the update window's red lines said what stopped it.").arg(now.toHtmlEscaped()),
                           10, theme::c().textBody);
        auto *way = text(QStringLiteral("If it stops again: download the <a href='%1' style='%3'>%2 setup</a>, unzip it "
                                        "and run install.bat. It installs over this one; your playlists, settings and "
                                        "mpv.conf stay. <a href='%4' style='%3'>Release page</a>")
                             .arg(setup, wanted.toHtmlEscaped(), link, page),
                         10, theme::c().textBody);
        way->setTextFormat(Qt::RichText);
        way->setOpenExternalLinks(true);
        auto *later = new QPushButton("Close");
        auto *again = new QPushButton("Try again");
        const QString shape = QStringLiteral("min-height: 36px; max-height: 36px; padding: 0 20px; border-radius: 9px;"
                                             " font-size: 10.5pt; font-weight: 600;");
        theme::style(later, QStringLiteral("QPushButton { %1 color: %2; background: transparent; border: 1px solid @tick; }"
                                           "QPushButton:hover { background: @raised; }").arg(shape, theme::c().textBright.name()));
        theme::style(again, QStringLiteral("QPushButton { %1 color: @onAccent; background: %2; border: none; }"
                                           "QPushButton:hover { background: @accentHover; }").arg(shape, theme::accent().name()));
        for (QPushButton *b : {later, again})
            b->setCursor(Qt::PointingHandCursor);
        connect(later, &QPushButton::clicked, this, &QDialog::reject);
        connect(again, &QPushButton::clicked, this, &QDialog::accept);
        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(10);
        buttons->addStretch();
        buttons->addWidget(later);
        buttons->addWidget(again);
        auto *box = new QVBoxLayout(this);
        box->setContentsMargins(24, 18, 24, 22);
        box->setSpacing(0);
        box->addWidget(title);
        box->addSpacing(8);
        box->addWidget(about);
        box->addSpacing(10);
        box->addWidget(way);
        box->addSpacing(18);
        box->addLayout(buttons);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(theme::c().line, 1));
        p.setBrush(theme::c().popup);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 14, 14);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton)
            m_dragFrom = event->globalPosition().toPoint() - frameGeometry().topLeft();
        QDialog::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if ((event->buttons() & Qt::LeftButton) && !m_dragFrom.isNull())
            move(event->globalPosition().toPoint() - m_dragFrom);
    }

private:
    QPoint m_dragFrom;
};

} // namespace

bool UpdateDialog::showIfUnfinished(QWidget *parent)
{
    QSettings settings;
    const QString wanted = settings.value("updates/pending").toString();
    const QString page = settings.value("updates/pendingPage").toString();
    settings.remove("updates/pending");
    settings.remove("updates/pendingPage");
    const QString now = QStringLiteral(GHOST_VERSION);
    if (wanted.isEmpty() || QVersionNumber::fromString(now) >= QVersionNumber::fromString(wanted))
        return false;
    UnfinishedDialog dialog(now, wanted,
                            page.isEmpty() ? QStringLiteral("https://github.com/TheTakanosu/Ghost-Engine-MPV-Setup/releases/tag/v%1").arg(wanted)
                                           : page,
                            parent);
    return dialog.exec() == QDialog::Accepted;
}

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
        // What it is meant to become, for the app that opens afterwards: still
        // older than this, it says so and shows the way on (showIfUnfinished).
        for (const auto &[part, tick] : m_rows)
            if (part.key == "ghost" && !part.latest.isEmpty()) {
                QSettings().setValue("updates/pending", part.latest);
                QSettings().setValue("updates/pendingPage", part.notes);
            }
        if (QProcess::startDetached("powershell.exe", args)) {
            accept();
            if (onQuitForUpdate)
                onQuitForUpdate();
        } else {
            QSettings().remove("updates/pending");
            QSettings().remove("updates/pendingPage");
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
        // update.ps1 exits with 1 when a part was not updated (1.6; before, 0 always).
        m_status->setText(code == 0 ? "Done. A list that is playing picks the new yt-dlp up from its next song."
                                    : "Something was not updated; the lines above say why. Try again later, or run "
                                      "update.bat in the mpv folder.");
        m_status->show();
        m_update->hide();
        m_later->setText("Close");
        m_later->setEnabled(true);
    });
    m_process->start("powershell.exe", updates::powershell({"-Only", keys.join(','), "-Unattended"}));
}

// -- what's new ------------------------------------------------------------------

namespace {

// What each version brings, as its window says it, oldest first. Someone
// coming from further back sees every version's lines since theirs; a
// version with nothing to tell (1.3.2 only mends the installer) adds none.
const QList<QPair<QString, QStringList>> &allNotes()
{
    static const QList<QPair<QString, QStringList>> notes = {
        {QStringLiteral("1.3.1"), {
            "Themes: Purple, Black, Light, or Match Windows. Settings > Appearance.",
            "History on your profile, and your top videos apart from your top songs.",
            "The playlist you play moves to the top of your library.",
            "Pick up where you left off: what played last waits in the player bar.",
            "Fixed: a video list opened after songs played as videos had no picture.",
        }},
        {QStringLiteral("1.3.3"), {
            "Maximised on your second screen, the app now stays there when you minimise it and bring it back (Win+number, the taskbar).",
            "Song lists play with no mpv window by default on new installs. Yours stays as you had it.",
            "Installing and updating work on older Windows 10, and keep going when GitHub's download link fails.",
        }},
        {QStringLiteral("1.3.4"), {
            "Search YouTube Music from the box at the top: a song plays with its radio, a playlist can be previewed and saved to your library. Paste a YouTube link to open it.",
            "Your library has its own search, and long titles are shortened instead of cut.",
            "Accent colour of your own: follow the theme, pick one of eight, or set any RGB/hex. Settings > Appearance.",
            "Keyboard shortcuts page (Ctrl+/): change any key, mouse back/forward buttons included.",
            "Settings: how far the arrow keys seek, subtitles on or off with your language order, and opening Ghost Engine when Windows starts.",
        }},
        {QStringLiteral("1.4.0"), {
            "A YouTube side: the video button at the top switches between Music and YouTube, each with its own menu. Home, History, Watch later and your video lists.",
            "Search all of YouTube, also from the middle of the window (Ctrl+K), with what you searched before and suggestions. Settings > Appearance picks the country results are ranked for.",
            "Follow channels: right-click a video or use Follow under it. Following shows their latest videos, a channel's picture opens its page, and a channel live now is marked in red.",
            "A channel's own page: its banner, picture, about, links and videos, its stream first when it is live. The folded YouTube menu shows the channels you follow as pictures.",
            "Back and Forward at the top left, also Alt+Left / Alt+Right and a mouse's side buttons, through lists, searches, channels and pages in both layouts.",
            "A \"Now playing\" island at the top while you are away from the video: hover it for the picture, the controls, Open and Mini player.",
            "Streams on air open at what is on air now, sound and picture in step. Paused or wound back, \"Go live\" takes you back. A radio played as sound starts live too.",
            "The quality menu offers only the qualities the video has.",
            "Liked songs, with a cover of its own: the + beside the playing song's title, or beside a song's length in a list, adds it; a tick takes it out. Right-click the + to add the song to any playlist.",
            "A song's radio from search opens as a page from the cover in the player bar, with its own cover and Back to results, and Save to library keeps it as Ghost Engine Radio #1, #2...",
            "Right-click a song for Add to queue, Play next, Go to song radio and Copy song link.",
            "Autoplay: when a list ends with repeat off, similar songs play on. Settings > Playback.",
            "Normalize volume evens out loud and quiet songs, on by default. Settings > Playback.",
            "Covers fill their square, without the flat bands YouTube puts beside them.",
            "The mini player has YouTube's controls: back and forward, a seek bar, the time, the sound, subtitles and full screen. It comes back after full screen, and I opens it after a click on the video.",
            "Without a sound device (speakers asleep, headset off) songs no longer race past; pick up where you left off follows radios and the queue too.",
            "Play works the first time after Ghost Engine opens, from a list or a search.",
            "In a small window Expand library and the YouTube menu make the window wider instead of flicking back.",
            "Leaving a video keeps the sound going without a corner player; Settings > Player brings the old way back.",
            "Opened as Windows starts, Ghost Engine waits for the network instead of searching forever, and says when YouTube can't be reached.",
            "The Search shortcut can be changed in Keyboard shortcuts like every other; Escape closes the box in the middle.",
            "The switch between Music and YouTube plays a short ghost intro. Settings > Appearance can make it a slide or instant.",
            "A video whose picture stopped while the sound went on now asks for its picture again by itself.",
            "Lists and sliders in Settings no longer change when the page is scrolled over them.",
        }},
        {QStringLiteral("1.5.0"), {
            "Artist pages: the artist's photo, their most played songs, albums and singles, a Radio and an About card. Open one from the artist's name in the player bar or Now playing, or Go to artist in a song's menu.",
            "Album pages: play an album in order or save it to your library. Search now shows Artists and Albums too.",
            "Recommended under your song lists: songs that fit the list and aren't in it yet. + adds one, a double-click plays it with its radio. Pull the panel up by its grip, or scroll to the end of the list to open it.",
            "Give a playlist a description: click under its name. It is kept in the list's file, so it goes with it when you share it.",
            "Import from Spotify (Library + > Import from Spotify...): paste a playlist or album link, or a list of track links copied from Spotify. No account or key needed.",
            "A ten-band equalizer with presets. Settings > Playback.",
            "Sleep timer: right-click the play button to pause after 5 to 90 minutes or at the end of the song.",
            "Private session: plays stay out of your history and top songs, and Discord shows nothing. Settings > Privacy or right-click your profile. It ends when the app closes.",
            "Music and YouTube searches are kept apart: each side searches its own, with its own earlier searches.",
            "The YouTube Home is built from what you search and watch, a few videos from each channel, instead of filling up with the last channel you watched.",
            "The video page shows the channel's picture and name, with LIVE when it is on air. Click the picture to open the channel.",
            "The floating island shows for songs too, on both the Music and the YouTube side.",
            "Settings > Appearance: \"Use Ghost Engine for\" Music and YouTube, Music only or YouTube only, and a switch for the floating island.",
            "Channel pages scroll smoothly: the header slides away and a one-row header with the picture, name and Follow takes its place.",
            "Live streams stay marked LIVE while they play normally, and Go live no longer shows all the time.",
            "Light theme: the cards have a soft shadow on all four sides.",
            "Followed channels' pictures are kept, so they show at once when the app opens, even before the network is up.",
            "Clicking an empty spot leaves the text box you were typing in.",
            "Fixes: searching again after opening an artist shows the new results; songs no longer skip to the end one after another after a resume; the library highlights the list you opened from the player bar or the island; cleaner corners and a centred + on songs; artist names are underlined under the mouse.",
        }},
    };
    return notes;
}

// The versions since seen up to now with their lines, newest first.
QList<QPair<QString, QStringList>> notesSince(const QString &seen, const QString &now)
{
    const auto older = [](const QString &a, const QString &b) {
        return QVersionNumber::fromString(a) < QVersionNumber::fromString(b);
    };
    QList<QPair<QString, QStringList>> out;
    for (const auto &[version, said] : allNotes())
        if ((seen.isEmpty() || older(seen, version)) && !older(now, version) && !said.isEmpty())
            out.prepend({version, said});
    return out;
}

} // namespace

void WhatsNewDialog::showIfUpdated(QWidget *parent, const std::function<void(QAbstractScrollArea *)> &smooth)
{
    QSettings settings;
    const QString now = QStringLiteral(GHOST_VERSION);
    const QString seen = settings.value("whatsnew/seen").toString();
    // Before 1.3.1 nothing was written here: settings of any other kind
    // mean the app ran before, so this is an update, not an install.
    const bool ranBefore = !seen.isEmpty() || settings.contains("window/geometry");
    settings.setValue("whatsnew/seen", now);
    const auto notes = notesSince(seen, now);
    if (seen == now || !ranBefore || notes.isEmpty())
        return;
    WhatsNewDialog dialog(now, notes, parent, smooth);
    dialog.exec();
}

WhatsNewDialog::WhatsNewDialog(const QString &version, const QList<QPair<QString, QStringList>> &versions, QWidget *parent,
                               const std::function<void(QAbstractScrollArea *)> &smooth)
    : QDialog(parent)
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
    // The lines scroll in a box of their own between the title and the
    // button, so the window fits its screen however many versions it tells
    // (1.4.0 and 1.5.0 together run past a 1080 px screen).
    auto *list = new QWidget;
    list->setAttribute(Qt::WA_TranslucentBackground);
    auto *listBox = new QVBoxLayout(list);
    listBox->setContentsMargins(0, 0, 8, 0);   // clear of the scroll bar
    listBox->setSpacing(8);
    // Several versions: each block under its own number, newest on top
    // (1.3.4 to 1.5.0 showed 1.4.0's lines first, all as one list).
    for (int v = 0; v < versions.size(); ++v) {
        if (versions.size() > 1) {
            if (v > 0)
                listBox->addSpacing(10);
            auto *heading = text(versions[v].first, 10, theme::c().textBody, QFont::Bold);
            heading->setTextFormat(Qt::PlainText);
            listBox->addWidget(heading);
        }
        for (const QString &line : versions[v].second) {
            auto *row = new QHBoxLayout;
            row->setSpacing(10);
            auto *dot = text(QStringLiteral("•"), 10.5, theme::accent(), QFont::Bold);
            dot->setWordWrap(false);
            auto *words = text(line, 10.5, theme::c().textBright);
            words->setTextFormat(Qt::PlainText);
            row->addWidget(dot, 0, Qt::AlignTop);
            row->addWidget(words, 1, Qt::AlignTop);
            listBox->addLayout(row);
        }
    }
    auto *scroll = new QScrollArea;
    scroll->setWidget(list);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
    scroll->viewport()->setAutoFillBackground(false);
    if (smooth)
        smooth(scroll);
    box->addWidget(scroll, 1);
    box->addSpacing(8);
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

    // As tall as the lines need, but at most 80% of the screen it opens on,
    // and placed over the window inside that screen.
    QScreen *screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
    const QRect room = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
    const int listWidth = width() - box->contentsMargins().left() - box->contentsMargins().right();
    const int listHeight = list->heightForWidth(listWidth) > 0 ? list->heightForWidth(listWidth) : list->sizeHint().height();
    scroll->setFixedHeight(listHeight);
    const int rest = box->sizeHint().height() - listHeight;
    const int most = room.height() * 8 / 10;
    if (rest + listHeight > most)
        scroll->setFixedHeight(qMax(80, most - rest));
    setFixedHeight(qMin(most, box->sizeHint().height()));
    const QRect over = parent ? parent->window()->frameGeometry() : room;
    QRect where(QPoint(0, 0), size());
    where.moveCenter(over.center());
    where.moveLeft(qBound(room.left(), where.left(), room.right() + 1 - where.width()));
    where.moveTop(qBound(room.top(), where.top(), room.bottom() + 1 - where.height()));
    move(where.topLeft());
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
