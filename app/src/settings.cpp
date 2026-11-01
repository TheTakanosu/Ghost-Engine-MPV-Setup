#include "settings.h"

#include "theme.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

// -- mpv.conf ------------------------------------------------------------------

namespace mpvconf {

namespace {

const QString kSection = QStringLiteral("# [Ghost Engine app] Set from the app's Settings page.");

QStringList readLines(bool *crlf)
{
    QFile file(path());
    QByteArray raw;
    if (file.open(QIODevice::ReadOnly))
        raw = file.readAll();
    *crlf = raw.isEmpty() || raw.contains("\r\n");
    QString text = QString::fromUtf8(raw);
    if (text.endsWith('\n'))
        text.chop(text.endsWith(QStringLiteral("\r\n")) ? 2 : 1);
    return text.isEmpty() ? QStringList() : text.split(QRegularExpression(QStringLiteral("\r?\n")));
}

bool writeLines(const QStringList &lines, bool crlf)
{
    QSaveFile file(path());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QString eol = crlf ? QStringLiteral("\r\n") : QStringLiteral("\n");
    file.write((lines.join(eol) + eol).toUtf8());
    return file.commit();
}

// The line that sets `key`, commented-out ones aside; -1 if none.
int find(const QStringList &lines, const QRegularExpression &line)
{
    for (int i = 0; i < lines.size(); ++i)
        if (line.match(lines[i]).hasMatch())
            return i;
    return -1;
}

QRegularExpression keyLine(const QString &key)
{
    return QRegularExpression(QStringLiteral("^\\s*%1\\s*=(.*)$").arg(QRegularExpression::escape(key)));
}

QRegularExpression optionLine(const QString &name)
{
    return QRegularExpression(QStringLiteral("^\\s*script-opts-append\\s*=\\s*%1=(.*)$")
                                  .arg(QRegularExpression::escape(name)));
}

// New lines go under the app's own heading at the end, made once.
void addLine(QStringList &lines, const QString &line)
{
    int at = lines.indexOf(kSection);
    if (at < 0) {
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty())
            lines << QString();
        lines << kSection;
        at = int(lines.size()) - 1;
    }
    int end = at + 1;
    while (end < lines.size() && !lines[end].trimmed().isEmpty() && !lines[end].startsWith('#'))
        ++end;
    lines.insert(end, line);
}

} // namespace

QString path()
{
    // Where the app finds mpv.exe: beside itself, or the default install.
    for (const QString &dir : {QCoreApplication::applicationDirPath(), QStringLiteral("C:/mpv")})
        if (QFileInfo::exists(QDir(dir).filePath("mpv.exe")))
            return QDir(dir).filePath("mpv.conf");
    return QStringLiteral("C:/mpv/mpv.conf");
}

QString value(const QString &key)
{
    bool crlf;
    const QStringList lines = readLines(&crlf);
    const int at = find(lines, keyLine(key));
    return at < 0 ? QString() : keyLine(key).match(lines[at]).captured(1).trimmed();
}

bool setValue(const QString &key, const QString &value)
{
    bool crlf;
    QStringList lines = readLines(&crlf);
    const QString line = key + '=' + value;
    const int at = find(lines, keyLine(key));
    if (at >= 0)
        lines[at] = line;
    else
        addLine(lines, line);
    return writeLines(lines, crlf);
}

QString option(const QString &name)
{
    bool crlf;
    const QStringList lines = readLines(&crlf);
    const int at = find(lines, optionLine(name));
    return at < 0 ? QString() : optionLine(name).match(lines[at]).captured(1).trimmed();
}

bool setOption(const QString &name, const QString &value)
{
    bool crlf;
    QStringList lines = readLines(&crlf);
    const int at = find(lines, optionLine(name));
    const QString line = QStringLiteral("script-opts-append=") + name + '=' + value;
    if (value.isEmpty()) {
        if (at < 0)
            return true;
        lines.removeAt(at);
        // The last of the app's lines gone: its heading goes too, with the
        // blank line before it, so the file is as it was.
        const int heading = lines.indexOf(kSection);
        if (heading >= 0 && (heading + 1 >= lines.size() || lines[heading + 1].trimmed().isEmpty() ||
                             lines[heading + 1].startsWith('#'))) {
            lines.removeAt(heading);
            if (heading > 0 && lines[heading - 1].trimmed().isEmpty())
                lines.removeAt(heading - 1);
        }
    } else if (at >= 0) {
        lines[at] = line;
    } else {
        addLine(lines, line);
    }
    return writeLines(lines, crlf);
}

} // namespace mpvconf

// -- switch --------------------------------------------------------------------

ToggleSwitch::ToggleSwitch(QWidget *parent) : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? theme::accent() : QColor(60, 58, 72));
    p.drawRoundedRect(track, track.height() / 2, track.height() / 2);
    const qreal knob = track.height() - 6;
    const qreal x = isChecked() ? track.right() - 3 - knob : track.left() + 3;
    p.setBrush(isChecked() ? QColor(24, 18, 34) : QColor(200, 198, 210));
    p.drawEllipse(QRectF(x, track.top() + 3, knob, knob));
}

// -- page ----------------------------------------------------------------------

namespace {

// The height a video may have; 0 is no cap.
const int kHeights[] = {0, 2160, 1440, 1080, 720, 480};

QString formatFor(int height)
{
    return height > 0 ? QStringLiteral("bestvideo[height<=%1]+bestaudio/best").arg(height)
                      : QStringLiteral("bestvideo+bestaudio/best");
}

QWidget *card(const QString &title, QVBoxLayout *&rows)
{
    auto *box = new QWidget;
    box->setObjectName("statCard");
    box->setAttribute(Qt::WA_StyledBackground);
    rows = new QVBoxLayout(box);
    rows->setContentsMargins(22, 18, 22, 18);
    rows->setSpacing(16);
    auto *heading = new QLabel(title);
    heading->setObjectName("sectionTitle");
    rows->addWidget(heading);
    return box;
}

// A setting: its name and a line on what it does, the control on the right.
void addRow(QVBoxLayout *rows, const QString &name, const QString &about, QWidget *control)
{
    auto *label = new QLabel(name);
    label->setObjectName("settingName");
    auto *text = new QLabel(about);
    text->setObjectName("statLabel");
    text->setWordWrap(true);
    auto *words = new QVBoxLayout;
    words->setSpacing(2);
    words->addWidget(label);
    words->addWidget(text);
    auto *row = new QHBoxLayout;
    row->setSpacing(24);
    row->addLayout(words, 1);
    row->addWidget(control, 0, Qt::AlignVCenter);
    rows->addLayout(row);
}

} // namespace

namespace performance {

const Level &level(int index)
{
    // High is what mpv.conf always had: three minutes or so of 1080p either
    // side of where it plays, so seeking there is instant. Balanced keeps
    // two-thirds of it, Low a short stretch (a seek back further loads again).
    static const Level levels[kLevels] = {
        {"Low", "20M", "10M", 20, 10},
        {"Balanced", "50M", "50M", 60, 20},
        {"High", "75M", "75M", 60, 30},
    };
    return levels[qBound(0, index, kLevels - 1)];
}

} // namespace performance

// The zoom as Discord shows it: the steps marked above a bar, 100 in the
// accent colour, the handle dragged or the bar clicked to a step.
class ZoomBar : public QWidget {
public:
    std::function<void(qreal)> onPicked;

    explicit ZoomBar(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(46);
        setCursor(Qt::PointingHandCursor);
    }
    void setZoom(qreal zoom)
    {
        m_step = qBound(0, qRound((zoom - kFirst) * 10), kSteps - 1);
        update();
    }

protected:
    static constexpr qreal kFirst = 0.7;
    static constexpr int kSteps = 10;   // 70 % to 160 %

    qreal xOf(int step) const { return 12 + (width() - 24) * step / qreal(kSteps - 1); }
    int stepAt(qreal x) const { return qBound(0, qRound((x - 12) / (width() - 24) * (kSteps - 1)), kSteps - 1); }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QFont small = font();
        small.setPointSizeF(font().pointSizeF() * 0.8);
        p.setFont(small);
        const qreal line = height() - 12;
        for (int i = 0; i < kSteps; ++i) {
            const int percent = qRound((kFirst + i / 10.0) * 100);
            const QRectF text(xOf(i) - 20, 0, 40, line - 12);
            p.setPen(percent == 100 ? theme::accent() : (i == m_step ? QColor(236, 236, 241) : QColor(150, 148, 162)));
            p.drawText(text, Qt::AlignHCenter | Qt::AlignTop, QString::number(percent));
            p.fillRect(QRectF(xOf(i) - 0.5, line - 9, 1, 5), QColor(90, 88, 104));
        }
        const QRectF groove(12, line - 2, width() - 24, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(60, 58, 72));
        p.drawRoundedRect(groove, 2, 2);
        p.setBrush(theme::accent());
        p.drawRoundedRect(QRectF(12, line - 2, xOf(m_step) - 12, 4), 2, 2);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(xOf(m_step), line), 8, 8);
    }
    void mousePressEvent(QMouseEvent *e) override { move(e->position().x()); }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (e->buttons() & Qt::LeftButton)
            move(e->position().x());
    }
    // Applied when let go: every step of the whole app laid out again is too
    // much to do while the handle is dragged.
    void mouseReleaseEvent(QMouseEvent *) override
    {
        if (onPicked)
            onPicked(kFirst + m_step / 10.0);
    }

private:
    void move(qreal x)
    {
        m_step = stepAt(x);
        update();
    }
    int m_step = 3;
};

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent)
{
    auto *title = new QLabel("Settings");
    title->setObjectName("pageTitle");

    // Playback: what mpv asks YouTube for. These live in mpv.conf, so an mpv
    // opened from Explorer or Discord follows them too.
    QVBoxLayout *playback;
    auto *playbackCard = card("Playback", playback);
    m_video = new QComboBox;
    for (int h : kHeights)
        m_video->addItem(h > 0 ? QStringLiteral("Up to %1p").arg(h) : QStringLiteral("Best available"), h);
    m_video->setMinimumWidth(190);
    addRow(playback, "Video quality",
           "The highest resolution a video plays at. Lower uses less data and stalls less on a slow connection.", m_video);
    m_autoQuality = new ToggleSwitch;
    addRow(playback, "Lower quality on a slow connection",
           "When a video keeps stopping to load, step down to 720p, then 480p, until mpv is closed.", m_autoQuality);
    m_audio = new QComboBox;
    m_audio->addItem("Best", QString());
    m_audio->addItem("Standard", QStringLiteral("standard"));
    m_audio->addItem("Data saver", QStringLiteral("saver"));
    m_audio->setMinimumWidth(190);
    addRow(playback, "Song quality",
           "The sound of song lists. Standard is about 128 kbps, Data saver about 64 kbps; Best is YouTube's highest.", m_audio);
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 130);
    m_volume->setFixedWidth(150);
    m_volumeValue = new QLabel;
    m_volumeValue->setObjectName("statLabel");
    m_volumeValue->setFixedWidth(34);
    auto *volumeBox = new QWidget;
    auto *volumeRow = new QHBoxLayout(volumeBox);
    volumeRow->setContentsMargins(0, 0, 0, 0);
    volumeRow->addWidget(m_volume);
    volumeRow->addWidget(m_volumeValue);
    addRow(playback, "Starting volume", "The volume mpv opens at.", volumeBox);

    // The player window (Batın's "app only / mpv only").
    QVBoxLayout *window;
    auto *windowCard = card("Player", window);
    m_window = new ToggleSwitch;
    addRow(window, "Show the mpv window for song lists",
           "Off: songs play inside Ghost Engine with no extra window, and closing Ghost Engine stops them. "
           "Video lists follow the setting below.", m_window);
    m_embedVideo = new ToggleSwitch;
    addRow(window, "Play videos inside Ghost Engine",
           "Video lists play on a page of the app, with the list beside the video. "
           "Off: they open in mpv's own window. Either way it is mpv playing.", m_embedVideo);
    connect(m_embedVideo, &QAbstractButton::toggled, this, [this](bool on) {
        if (!m_loading)
            QSettings().setValue("player/embedVideo", on);
    });

    QVBoxLayout *downloads;
    auto *downloadsCard = card("Downloads", downloads);
    m_offlineQuality = new QComboBox;
    m_offlineQuality->addItem("Best", QString());
    m_offlineQuality->addItem("Standard", QStringLiteral("standard"));
    m_offlineQuality->addItem("Data saver", QStringLiteral("saver"));
    m_offlineQuality->setMinimumWidth(190);
    addRow(downloads, "Download quality",
           "The sound of songs downloaded from now on. Only songs are downloaded, as YouTube serves them; "
           "they play from your disk, at once and without the internet.", m_offlineQuality);
    // One row per downloaded list, between the quality and the total.
    m_listsBox = new QWidget;
    auto *listsLayout = new QVBoxLayout(m_listsBox);
    listsLayout->setContentsMargins(0, 0, 0, 0);
    listsLayout->setSpacing(8);
    downloads->addWidget(m_listsBox);
    m_offlineUsed = new QLabel;
    m_offlineUsed->setObjectName("statLabel");
    auto *clear = new QPushButton("Remove all downloads");
    clear->setObjectName("chip");
    clear->setCursor(Qt::PointingHandCursor);
    connect(clear, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, "Remove all downloads",
                "Delete every downloaded song? The playlists stay; their songs play from YouTube again.") != QMessageBox::Yes)
            return;
        if (onRemoveDownloads)
            onRemoveDownloads();
        load();
    });
    addRow(downloads, "On this computer", QString(), clear);
    // The summary sits under "On this computer", where the description goes.
    {
        auto *row = downloads->itemAt(downloads->count() - 1)->layout();
        auto *words = row->itemAt(0)->layout();
        static_cast<QLabel *>(words->itemAt(1)->widget())->hide();
        words->addWidget(m_offlineUsed);
    }
    connect(m_offlineQuality, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_loading)
            QSettings().setValue("offline/quality", m_offlineQuality->currentData().toString());
    });

    QVBoxLayout *look;
    auto *lookCard = card("Appearance", look);
    // The zoom bar takes the card's whole width under its name.
    {
        auto *name = new QLabel("Zoom");
        name->setObjectName("settingName");
        auto *about = new QLabel("The size of everything in the app. Ctrl + and Ctrl -, or Ctrl and the mouse wheel, change it too.");
        about->setObjectName("statLabel");
        about->setWordWrap(true);
        m_zoom = new ZoomBar;
        m_zoom->onPicked = [this](qreal zoom) {
            if (onZoomChanged)
                onZoomChanged(zoom);
        };
        auto *words = new QVBoxLayout;
        words->setSpacing(2);
        words->addWidget(name);
        words->addWidget(about);
        words->addSpacing(6);
        words->addWidget(m_zoom);
        look->addLayout(words);
    }
    m_covers = new QComboBox;
    m_covers->addItem("Rounded corners", false);
    m_covers->addItem("Circles", true);
    m_covers->setMinimumWidth(190);
    addRow(look, "Playlist covers", "The shape of playlist covers in the library, on Home and on a playlist's page.", m_covers);
    m_volumeNumber = new ToggleSwitch;
    addRow(look, "Show the volume as a number", "A percentage next to the volume bar.", m_volumeNumber);

    QVBoxLayout *speed;
    auto *speedCard = card("Performance", speed);
    m_memory = new QComboBox;
    for (int i = 0; i < performance::kLevels; ++i)
        m_memory->addItem(performance::level(i).name, i);
    m_memory->setMinimumWidth(190);
    addRow(speed, "Memory use",
           "How much of a video is kept around where it plays, and how many covers the app keeps ready. "
           "High seeks a few minutes either way at once; Low uses the least memory and loads again "
           "when you seek further.", m_memory);
    m_memoryNow = new QLabel;
    m_memoryNow->setObjectName("statLabel");
    m_memoryNow->setWordWrap(true);
    addRow(speed, "Right now", QString(), new QWidget);
    {
        // The figures stand where the description goes.
        auto *row = speed->itemAt(speed->count() - 1)->layout();
        auto *words = row->itemAt(0)->layout();
        static_cast<QLabel *>(words->itemAt(1)->widget())->hide();
        words->addWidget(m_memoryNow);
    }
    m_memoryTimer = new QTimer(this);
    m_memoryTimer->setInterval(2000);
    connect(m_memoryTimer, &QTimer::timeout, this, [this] {
        m_memoryNow->setText(memoryUsage ? memoryUsage() : QString());
    });
    connect(m_memory, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading || m_memory->currentData().toInt() < 0)
            return;
        const int index = m_memory->currentData().toInt();
        const performance::Level &l = performance::level(index);
        mpvconf::setValue("demuxer-max-bytes", QString::fromLatin1(l.ahead));
        mpvconf::setValue("demuxer-max-back-bytes", QString::fromLatin1(l.back));
        mpvconf::setValue("demuxer-readahead-secs", QString::number(l.aheadSecs));
        QSettings().setValue("performance/memory", index);
        if (onMemoryChanged)
            onMemoryChanged(index);
    });

    QVBoxLayout *discord;
    auto *discordCard = card("Discord", discord);
    m_presenceSongs = new ToggleSwitch;
    addRow(discord, "Show songs on Discord", "What you listen to, on your Discord profile (Ghost RPC).", m_presenceSongs);
    m_presenceVideos = new ToggleSwitch;
    addRow(discord, "Show videos on Discord", "What you watch, on your Discord profile (Ghost RPC).", m_presenceVideos);

    m_confPath = new QLabel;
    m_confPath->setObjectName("statLabel");
    m_confPath->setWordWrap(true);
    auto *openConf = new QPushButton("Open mpv.conf");
    openConf->setObjectName("link");
    openConf->setCursor(Qt::PointingHandCursor);
    connect(openConf, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(mpvconf::path()));
    });
    auto *footer = new QHBoxLayout;
    footer->addWidget(m_confPath, 1);
    footer->addWidget(openConf);

    auto *content = new QWidget;
    content->setObjectName("home");
    auto *box = new QVBoxLayout(content);
    box->setContentsMargins(28, 22, 28, 24);
    box->setSpacing(16);
    box->addWidget(title);
    box->addWidget(playbackCard);
    box->addWidget(windowCard);
    box->addWidget(downloadsCard);
    box->addWidget(lookCard);
    box->addWidget(speedCard);
    box->addWidget(discordCard);
    box->addLayout(footer);
    box->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    // Saved as soon as they change.
    connect(m_video, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading || m_video->currentData().toInt() < 0)
            return;
        const QString format = formatFor(m_video->currentData().toInt());
        mpvconf::setValue("ytdl-format", format);
        if (onVideoFormatChanged)
            onVideoFormatChanged(format);
    });
    connect(m_autoQuality, &QAbstractButton::toggled, this, [this](bool on) {
        if (!m_loading)
            mpvconf::setOption("ghost-auto-quality", on ? QString() : QStringLiteral("no"));
    });
    connect(m_audio, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_loading)
            mpvconf::setOption("ghost-audio", m_audio->currentData().toString());
    });
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) { m_volumeValue->setText(QString::number(v)); });
    connect(m_volume, &QSlider::sliderReleased, this, [this] {
        mpvconf::setValue("volume", QString::number(m_volume->value()));
    });
    connect(m_window, &QAbstractButton::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        QSettings().setValue("player/window", on);
        if (onPlayerWindowChanged)
            onPlayerWindowChanged(on);
    });
    connect(m_covers, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading)
            return;
        QSettings().setValue("look/circleCovers", m_covers->currentData().toBool());
        if (onLookChanged)
            onLookChanged();
    });
    for (ToggleSwitch *toggle : {m_presenceSongs, m_presenceVideos})
        connect(toggle, &QAbstractButton::toggled, this, [this] {
            if (m_loading)
                return;
            QSettings().setValue("presence/songs", m_presenceSongs->isChecked());
            QSettings().setValue("presence/videos", m_presenceVideos->isChecked());
            if (onPresenceChanged)
                onPresenceChanged(m_presenceSongs->isChecked(), m_presenceVideos->isChecked());
        });
    connect(m_volumeNumber, &QAbstractButton::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        QSettings().setValue("look/volumeNumber", on);
        if (onLookChanged)
            onLookChanged();
    });
    load();
}

void SettingsPage::load()
{
    m_loading = true;
    // A format written by hand, not one of ours, is shown as such and left
    // alone until another is picked.
    const QString format = mpvconf::value("ytdl-format");
    const QRegularExpressionMatch cap = QRegularExpression(QStringLiteral(R"(^bestvideo\[height<=(\d+)\]\+bestaudio/best$)")).match(format);
    int index = -1;
    if (format.isEmpty() || format == formatFor(0))
        index = format.isEmpty() ? m_video->findData(1080) : 0;
    else if (cap.hasMatch())
        index = m_video->findData(cap.captured(1).toInt());
    const int custom = m_video->findData(-1);
    if (index < 0) {
        if (custom < 0)
            m_video->addItem("Custom (set in mpv.conf)", -1);
        index = m_video->findData(-1);
    } else if (custom >= 0) {
        m_video->removeItem(custom);
    }
    m_video->setCurrentIndex(index);
    m_autoQuality->setChecked(mpvconf::option("ghost-auto-quality") != "no");
    m_audio->setCurrentIndex(qMax(0, m_audio->findData(mpvconf::option("ghost-audio"))));
    bool ok = false;
    const int volume = mpvconf::value("volume").toInt(&ok);
    m_volume->setValue(ok ? volume : 100);
    m_volumeValue->setText(QString::number(m_volume->value()));
    QSettings settings;
    m_window->setChecked(settings.value("player/window", true).toBool());
    m_embedVideo->setChecked(settings.value("player/embedVideo", true).toBool());
    m_covers->setCurrentIndex(settings.value("look/circleCovers").toBool() ? 1 : 0);
    m_volumeNumber->setChecked(settings.value("look/volumeNumber", false).toBool());
    m_presenceSongs->setChecked(settings.value("presence/songs", true).toBool());
    m_presenceVideos->setChecked(settings.value("presence/videos", true).toBool());
    m_offlineQuality->setCurrentIndex(qMax(0, m_offlineQuality->findData(settings.value("offline/quality").toString())));
    m_zoom->setZoom(theme::zoom());
    // The level whose three values mpv.conf has; values set by hand show as such.
    int memory = -1;
    for (int i = 0; i < performance::kLevels; ++i) {
        const performance::Level &l = performance::level(i);
        if (mpvconf::value("demuxer-max-bytes") == QLatin1String(l.ahead) &&
            mpvconf::value("demuxer-max-back-bytes") == QLatin1String(l.back) &&
            mpvconf::value("demuxer-readahead-secs") == QString::number(l.aheadSecs))
            memory = i;
    }
    const int customMemory = m_memory->findData(-1);
    if (memory < 0 && customMemory < 0)
        m_memory->addItem("Custom (set in mpv.conf)", -1);
    else if (memory >= 0 && customMemory >= 0)
        m_memory->removeItem(customMemory);
    m_memory->setCurrentIndex(m_memory->findData(memory));
    m_memoryNow->setText(memoryUsage ? memoryUsage() : QString());
    m_offlineUsed->setText(downloadsSummary ? downloadsSummary() : QString());
    // The downloaded lists, built again each time the page is shown.
    QLayout *rows = m_listsBox->layout();
    while (QLayoutItem *item = rows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QList<DownloadedList> lists = downloadedLists ? downloadedLists() : QList<DownloadedList>();
    for (const DownloadedList &l : lists) {
        auto *row = new QWidget;
        row->setObjectName("downloadRow");
        row->setAttribute(Qt::WA_StyledBackground);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(10, 6, 10, 6);
        line->setSpacing(12);
        auto *cover = new QLabel;
        cover->setFixedSize(36, 36);
        cover->setPixmap(l.cover);
        auto *name = new QLabel(l.name);
        name->setObjectName("settingName");
        QString detail = QStringLiteral("%1 %2 · %3 MB").arg(l.songs).arg(l.songs == 1 ? "song" : "songs")
                             .arg(double(l.bytes) / (1024 * 1024), 0, 'f', 1);
        if (l.toGo > 0)
            detail += QStringLiteral(" · downloading, %1 to go").arg(l.toGo);
        auto *size = new QLabel(detail);
        size->setObjectName("statLabel");
        auto *words = new QVBoxLayout;
        words->setSpacing(0);
        words->addWidget(name);
        words->addWidget(size);
        auto *drop = new QPushButton(QString(QChar(0xE711)));   // a cross
        drop->setObjectName("glyph");
        drop->setToolTip("Remove this list's download");
        drop->setCursor(Qt::PointingHandCursor);
        const QString path = l.path, listName = l.name;
        connect(drop, &QPushButton::clicked, this, [this, path, listName] {
            if (QMessageBox::question(this, "Remove download",
                    QStringLiteral("Remove the downloaded songs of \"%1\"?").arg(listName)) != QMessageBox::Yes)
                return;
            if (onRemoveListDownload)
                onRemoveListDownload(path);
            load();
        });
        line->addWidget(cover);
        line->addLayout(words, 1);
        line->addWidget(drop);
        rows->addWidget(row);
    }
    m_listsBox->setVisible(!lists.isEmpty());
    m_confPath->setText(QStringLiteral("Playback settings are kept in %1, and updates keep them.")
                            .arg(QDir::toNativeSeparators(mpvconf::path())));
    m_loading = false;
}

void SettingsPage::showZoom(qreal zoom)
{
    m_zoom->setZoom(zoom);
}

void SettingsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_memoryTimer->start();   // the figures, only while they can be seen
}

void SettingsPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_memoryTimer->stop();
}
