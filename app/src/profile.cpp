#include "profile.h"
#include "theme.h"

#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtEndian>
#include <algorithm>

namespace {

// Ghost Engine's own Discord application, the one ghost_rpc.py presents as.
const char *kClientId = "1540451413065469992";

QString dataFile(const QString &name)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return dir + '/' + name;
}

QPixmap rounded(const QPixmap &source, int side, qreal radius, qreal dpr)
{
    // The middle square of the picture, cut round (radius = side / 2) or
    // round-cornered.
    QPixmap out(QSize(side, side) * dpr);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(0, 0, side, side), radius, radius);
    p.setClipPath(shape);
    if (source.isNull()) {
        p.fillRect(QRectF(0, 0, side, side), theme::c().raised);
    } else {
        const int edge = qMin(source.width(), source.height());
        p.drawPixmap(QRectF(0, 0, side, side), source,
                     QRect((source.width() - edge) / 2, (source.height() - edge) / 2, edge, edge));
    }
    return out;
}

QPixmap initials(const QString &name, int side, qreal dpr)
{
    // An artist with no photo yet: their first letter on a round tile.
    QPixmap out = rounded(QPixmap(), side, side / 2.0, dpr);
    QPainter p(&out);
    p.setRenderHint(QPainter::TextAntialiasing);
    QFont font;
    font.setPixelSize(side * 2 / 5);
    font.setWeight(QFont::Bold);
    p.setFont(font);
    p.setPen(theme::c().dim);
    p.drawText(QRectF(0, 0, side, side), Qt::AlignCenter, name.left(1).toUpper());
    return out;
}

QString clock(double seconds)
{
    const int s = qMax(0, int(seconds));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

} // namespace

// -- Discord -------------------------------------------------------------------

DiscordProfile::DiscordProfile(QObject *parent) : QObject(parent)
{
    QSettings settings;
    m_id = settings.value("profile/id").toString();
    m_name = settings.value("profile/name").toString();
    m_handle = settings.value("profile/handle").toString();
    m_avatarUrl = settings.value("profile/avatarUrl").toString();
    m_avatar.load(avatarPath());
    connect(&m_pipe, &QLocalSocket::connected, this, [this] {
        const QByteArray body = QJsonDocument(QJsonObject{{"v", 1}, {"client_id", kClientId}}).toJson(QJsonDocument::Compact);
        QByteArray frame(8, 0);
        qToLittleEndian<qint32>(0, frame.data());   // op 0: handshake
        qToLittleEndian<qint32>(body.size(), frame.data() + 4);
        m_pipe.write(frame + body);
    });
    connect(&m_pipe, &QLocalSocket::readyRead, this, &DiscordProfile::read);
    connect(&m_pipe, &QLocalSocket::errorOccurred, this, [this] {
        // Discord takes the first free pipe of ten; another client may hold
        // the first ones.
        if (m_pipe.state() == QLocalSocket::ConnectedState)
            return;
        if (++m_pipeNumber < 10)
            tryPipe();
    });
}

QString DiscordProfile::avatarPath()
{
    return dataFile(QStringLiteral("profile-avatar.png"));
}

void DiscordProfile::refresh()
{
    if (m_pipe.state() != QLocalSocket::UnconnectedState)
        return;
    m_pipeNumber = 0;
    tryPipe();
}

void DiscordProfile::tryPipe()
{
    m_buffer.clear();
    m_pipe.abort();
    m_pipe.connectToServer(QStringLiteral("discord-ipc-%1").arg(m_pipeNumber));
}

void DiscordProfile::read()
{
    m_buffer += m_pipe.readAll();
    if (m_buffer.size() < 8)
        return;
    const qint32 length = qFromLittleEndian<qint32>(m_buffer.constData() + 4);
    if (m_buffer.size() < 8 + length)
        return;
    const QJsonObject answer = QJsonDocument::fromJson(m_buffer.mid(8, length)).object();
    // Said hello, heard who it is: goodbye (op 2), so Discord keeps no
    // second presence for us.
    const QByteArray body = QJsonDocument(QJsonObject{{"v", 1}, {"client_id", kClientId}}).toJson(QJsonDocument::Compact);
    QByteArray frame(8, 0);
    qToLittleEndian<qint32>(2, frame.data());
    qToLittleEndian<qint32>(body.size(), frame.data() + 4);
    m_pipe.write(frame + body);
    m_pipe.flush();
    m_pipe.disconnectFromServer();

    const QJsonObject user = answer.value("data").toObject().value("user").toObject();
    if (answer.value("evt").toString() != QLatin1String("READY") || user.value("id").toString().isEmpty())
        return;
    m_id = user.value("id").toString();
    m_handle = user.value("username").toString();
    m_name = user.value("global_name").toString();
    if (m_name.isEmpty())
        m_name = m_handle;
    QSettings settings;
    settings.setValue("profile/id", m_id);
    settings.setValue("profile/name", m_name);
    settings.setValue("profile/handle", m_handle);
    emit changed();
    fetchAvatar(user.value("avatar").toString());
}

void DiscordProfile::fetchAvatar(const QString &hash)
{
    // No picture of their own: one of Discord's default ones, picked from
    // the id as Discord picks it.
    const QString url = hash.isEmpty()
        ? QStringLiteral("https://cdn.discordapp.com/embed/avatars/%1.png").arg((m_id.toULongLong() >> 22) % 6)
        : QStringLiteral("https://cdn.discordapp.com/avatars/%1/%2.png?size=256").arg(m_id, hash);
    m_avatarUrl = url;
    QSettings().setValue("profile/avatarUrl", url);
    QNetworkReply *reply = m_net.get(QNetworkRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        QPixmap picture;
        if (reply->error() != QNetworkReply::NoError || !picture.loadFromData(reply->readAll()))
            return;
        m_avatar = picture;
        picture.save(avatarPath(), "PNG");
        emit changed();
    });
}

QPixmap RemotePictures::get(const QString &url)
{
    if (!url.startsWith(QStringLiteral("https://")))
        return {};
    auto known = m_pictures.constFind(url);
    if (known != m_pictures.cend())
        return *known;
    m_pictures.insert(url, QPixmap());   // asked; empty until it arrives
    QNetworkReply *reply = m_net.get(QNetworkRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        QPixmap picture;
        if (reply->error() == QNetworkReply::NoError && picture.loadFromData(reply->readAll())) {
            m_pictures.insert(url, picture);
            emit ready(url);
        }
    });
    return {};
}

// -- listening -----------------------------------------------------------------

ListeningStats::ListeningStats()
{
    QFile file(dataFile(QStringLiteral("listening.json")));
    if (file.open(QIODevice::ReadOnly))
        m_data = QJsonDocument::fromJson(file.readAll()).object();
    if (!m_data.contains("since"))
        m_data["since"] = QDateTime::currentDateTime().toString(Qt::ISODate);
}

void ListeningStats::save() const
{
    QFile file(dataFile(QStringLiteral("listening.json")));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(m_data).toJson(QJsonDocument::Compact));
}

void ListeningStats::played(const QString &title, const QString &artist, const QString &id, const QString &list)
{
    // A song is the same whether its file or its video played.
    const QString key = (artist + QStringLiteral(" - ") + title).toLower();
    QJsonObject songs = m_data.value("songs").toObject();
    QJsonObject song = songs.value(key).toObject();
    song["title"] = title;
    song["artist"] = artist;
    if (!id.isEmpty())
        song["id"] = id;
    song["plays"] = song.value("plays").toInt() + 1;
    songs[key] = song;
    m_data["songs"] = songs;
    if (!artist.isEmpty()) {
        QJsonObject artists = m_data.value("artists").toObject();
        const QString who = artist.toLower();
        QJsonObject one = artists.value(who).toObject();
        one["name"] = artist;
        one["plays"] = one.value("plays").toInt() + 1;
        artists[who] = one;
        m_data["artists"] = artists;
    }
    if (!list.isEmpty()) {
        QJsonObject lists = m_data.value("lists").toObject();
        lists[list] = lists.value(list).toInt() + 1;
        m_data["lists"] = lists;
    }
    save();
}

QList<ListeningStats::Song> ListeningStats::topSongs(int count) const
{
    QList<Song> all;
    const QJsonObject songs = m_data.value("songs").toObject();
    for (auto it = songs.begin(); it != songs.end(); ++it) {
        const QJsonObject s = it.value().toObject();
        all.append({it.key(), s.value("title").toString(), s.value("artist").toString(),
                    s.value("id").toString(), s.value("plays").toInt()});
    }
    std::stable_sort(all.begin(), all.end(), [](const Song &a, const Song &b) { return a.plays > b.plays; });
    return all.mid(0, count);
}

QList<ListeningStats::Count> ListeningStats::topArtists(int count) const
{
    QList<Count> all;
    const QJsonObject artists = m_data.value("artists").toObject();
    for (const QJsonValue &v : artists)
        all.append({v.toObject().value("name").toString(), v.toObject().value("plays").toInt()});
    std::stable_sort(all.begin(), all.end(), [](const Count &a, const Count &b) { return a.plays > b.plays; });
    return all.mid(0, count);
}

QList<ListeningStats::Count> ListeningStats::topLists(int count) const
{
    QList<Count> all;
    const QJsonObject lists = m_data.value("lists").toObject();
    for (auto it = lists.begin(); it != lists.end(); ++it)
        all.append({it.key(), it.value().toInt()});
    std::stable_sort(all.begin(), all.end(), [](const Count &a, const Count &b) { return a.plays > b.plays; });
    return all.mid(0, count);
}

QDateTime ListeningStats::since() const
{
    return QDateTime::fromString(m_data.value("since").toString(), Qt::ISODate);
}

bool ListeningStats::empty() const
{
    return m_data.value("songs").toObject().isEmpty();
}

// -- page ----------------------------------------------------------------------

ProfilePage::ProfilePage(QWidget *parent) : QWidget(parent)
{
    setObjectName("home");

    // The card at the top, Discord's: round avatar, name, @handle.
    m_avatar = new QLabel;
    m_avatar->setFixedSize(132, 132);
    auto *kicker = new QLabel("PROFILE");
    kicker->setObjectName("kicker");
    m_name = new QLabel;
    m_name->setObjectName("profileName");
    m_info = new QLabel;
    m_info->setObjectName("statLabel");
    auto *who = new QVBoxLayout;
    who->setSpacing(2);
    who->addStretch();
    who->addWidget(kicker);
    who->addWidget(m_name);
    who->addWidget(m_info);
    who->addStretch();
    auto *head = new QHBoxLayout;
    head->setSpacing(24);
    head->addWidget(m_avatar);
    head->addLayout(who, 1);

    // What friends see on Discord, here too.
    m_nowCard = new QWidget;
    m_nowCard->setObjectName("statCard");
    m_nowCard->setAttribute(Qt::WA_StyledBackground);
    m_nowCard->setCursor(Qt::PointingHandCursor);
    m_nowCard->installEventFilter(this);
    m_nowCover = new QLabel;
    m_nowCover->setFixedSize(76, 76);
    m_nowKicker = new QLabel;
    m_nowKicker->setObjectName("kicker");
    m_nowTitle = new QLabel;
    m_nowTitle->setObjectName("videoTitle");
    m_nowArtist = new QLabel;
    m_nowArtist->setObjectName("videoChannel");
    m_nowFrom = new QLabel;
    m_nowFrom->setObjectName("statLabel");
    auto *nowText = new QVBoxLayout;
    nowText->setSpacing(2);
    nowText->addWidget(m_nowKicker);
    nowText->addWidget(m_nowTitle);
    nowText->addWidget(m_nowArtist);
    nowText->addWidget(m_nowFrom);
    auto *now = new QHBoxLayout(m_nowCard);
    now->setContentsMargins(14, 12, 18, 12);
    now->setSpacing(16);
    now->addWidget(m_nowCover);
    now->addLayout(nowText, 1);

    auto section = [](const QString &text) {
        auto *label = new QLabel(text);
        label->setObjectName("sectionTitle");
        return label;
    };
    m_artistsTitle = section("Top artists");
    m_artists = new QHBoxLayout;
    m_artists->setSpacing(18);
    m_songsTitle = section("Top songs");
    m_songs = new QVBoxLayout;
    m_songs->setSpacing(4);
    m_listsTitle = section("Most played playlists");
    m_listsBox = new QVBoxLayout;
    m_listsBox->setContentsMargins(0, 0, 0, 0);
    m_since = new QLabel;
    m_since->setObjectName("statLabel");
    m_since->setWordWrap(true);

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(28, 22, 28, 16);
    box->setSpacing(12);
    box->addLayout(head);
    box->addSpacing(6);
    box->addWidget(m_nowCard);
    box->addSpacing(8);
    box->addWidget(m_artistsTitle);
    box->addLayout(m_artists);
    box->addWidget(m_songsTitle);
    box->addLayout(m_songs);
    box->addWidget(m_listsTitle);
    box->addLayout(m_listsBox);
    box->addWidget(m_since);
    box->addStretch();
    setIdle();
}

void ProfilePage::setUser(const QString &name, const QString &handle, const QPixmap &avatar, const QString &counts)
{
    m_avatar->setPixmap(avatar.isNull() ? initials(name, 132, devicePixelRatioF())
                                        : rounded(avatar, 132, 66, devicePixelRatioF()));
    m_name->setText(name);
    m_info->setText(handle.isEmpty() ? counts : QStringLiteral("@") + handle + QStringLiteral("  ·  ") + counts);
}

void ProfilePage::setListening(const QPixmap &cover, const QString &title, const QString &artist,
                               const QString &list, bool paused)
{
    m_nowCover->setPixmap(rounded(cover, 76, 8, devicePixelRatioF()));
    m_nowKicker->setText(paused ? QStringLiteral("PAUSED") : QStringLiteral("LISTENING NOW"));
    m_nowTitle->setText(title);
    m_nowArtist->setText(artist);
    m_nowArtist->setVisible(!artist.isEmpty());
    m_nowList = list;
    m_nowPaused = paused;
    m_nowFrom->setText(list.isEmpty() ? QString() : QStringLiteral("from ") + list);
    m_nowFrom->setVisible(!list.isEmpty());
}

void ProfilePage::setPosition(double seconds, double duration)
{
    // "from IşıklarımOl  ·  1:23 / 3:08"
    QString text = m_nowList.isEmpty() ? QString() : QStringLiteral("from ") + m_nowList;
    if (duration > 0)
        text += (text.isEmpty() ? QString() : QStringLiteral("  ·  ")) + clock(seconds) + QStringLiteral(" / ") + clock(duration);
    m_nowFrom->setText(text);
    m_nowFrom->setVisible(!text.isEmpty());
}

void ProfilePage::setIdle()
{
    m_nowCover->setPixmap(rounded(QPixmap(), 76, 8, devicePixelRatioF()));
    m_nowKicker->setText(QStringLiteral("LISTENING NOW"));
    m_nowTitle->setText(QStringLiteral("Nothing is playing"));
    m_nowArtist->hide();
    m_nowList.clear();
    m_nowFrom->hide();
}

void ProfilePage::setTopArtists(const QList<Artist> &artists)
{
    while (QLayoutItem *item = m_artists->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const qreal dpr = devicePixelRatioF();
    for (const Artist &a : artists) {
        auto *tile = new QWidget;
        auto *photo = new QLabel;
        photo->setFixedSize(120, 120);
        photo->setPixmap(a.photo.isNull() ? initials(a.name, 120, dpr) : rounded(a.photo, 120, 60, dpr));
        auto *name = new QLabel(a.name);
        name->setObjectName("tileName");
        name->setFixedWidth(120);
        name->setText(name->fontMetrics().elidedText(a.name, Qt::ElideRight, 120));
        auto *plays = new QLabel(a.plays == 1 ? QStringLiteral("1 play") : QStringLiteral("%1 plays").arg(a.plays));
        plays->setObjectName("statLabel");
        auto *box = new QVBoxLayout(tile);
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(4);
        box->addWidget(photo);
        box->addWidget(name);
        box->addWidget(plays);
        m_artists->addWidget(tile);
    }
    m_artists->addStretch();
    m_artistsTitle->setVisible(!artists.isEmpty());
}

void ProfilePage::setTopSongs(const QList<Song> &songs)
{
    while (QLayoutItem *item = m_songs->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const qreal dpr = devicePixelRatioF();
    int number = 0;
    for (const Song &s : songs) {
        auto *row = new QWidget;
        auto *place = new QLabel(QString::number(++number));
        place->setObjectName("statLabel");
        place->setFixedWidth(22);
        place->setAlignment(Qt::AlignCenter);
        auto *cover = new QLabel;
        cover->setFixedSize(44, 44);
        cover->setPixmap(rounded(s.cover, 44, 5, dpr));
        auto *title = new QLabel(s.title);
        auto *artist = new QLabel(s.artist);
        artist->setObjectName("statLabel");
        auto *text = new QVBoxLayout;
        text->setSpacing(0);
        text->addWidget(title);
        text->addWidget(artist);
        auto *plays = new QLabel(s.plays == 1 ? QStringLiteral("1 play") : QStringLiteral("%1 plays").arg(s.plays));
        plays->setObjectName("statLabel");
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(4, 2, 12, 2);
        line->setSpacing(12);
        line->addWidget(place);
        line->addWidget(cover);
        line->addLayout(text, 1);
        line->addWidget(plays);
        m_songs->addWidget(row);
    }
    m_songsTitle->setVisible(!songs.isEmpty());
}

void ProfilePage::setListsShelf(QListWidget *shelf)
{
    m_listsBox->addWidget(shelf);
}

void ProfilePage::setListsVisible(bool visible)
{
    m_listsTitle->setVisible(visible);
    for (int i = 0; i < m_listsBox->count(); ++i)
        if (QWidget *w = m_listsBox->itemAt(i)->widget())
            w->setVisible(visible);
}

void ProfilePage::setSince(const QDateTime &since, bool empty)
{
    const QString date = QLocale().toString(since.date(), QLocale::LongFormat);
    m_since->setText(empty
        ? QStringLiteral("Your top artists and songs show here once you listen: a song counts after 30 seconds of it.")
        : QStringLiteral("Counted since %1. A song counts after 30 seconds of it.").arg(date));
}

bool ProfilePage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_nowCard && event->type() == QEvent::MouseButtonRelease && onOpenPlaying)
        onOpenPlaying();
    return QWidget::eventFilter(watched, event);
}
