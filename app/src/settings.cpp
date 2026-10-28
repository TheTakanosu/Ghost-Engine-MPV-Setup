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
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
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
           "Video lists still open in mpv's window.", m_window);

    QVBoxLayout *look;
    auto *lookCard = card("Look", look);
    m_covers = new QComboBox;
    m_covers->addItem("Rounded corners", false);
    m_covers->addItem("Circles", true);
    m_covers->setMinimumWidth(190);
    addRow(look, "Playlist covers", "The shape of playlist covers in the library, on Home and on a playlist's page.", m_covers);
    m_volumeNumber = new ToggleSwitch;
    addRow(look, "Show the volume as a number", "A percentage next to the volume bar.", m_volumeNumber);

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
    box->addWidget(lookCard);
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
    m_covers->setCurrentIndex(settings.value("look/circleCovers").toBool() ? 1 : 0);
    m_volumeNumber->setChecked(settings.value("look/volumeNumber", false).toBool());
    m_confPath->setText(QStringLiteral("Playback settings are kept in %1, and updates keep them.")
                            .arg(QDir::toNativeSeparators(mpvconf::path())));
    m_loading = false;
}
