#include "ytmusic.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace ytmusic {

namespace {

// The filters YouTube Music's own search tabs send.
QString filterFor(Kind kind)
{
    switch (kind) {
    case Kind::Song:     return QStringLiteral("EgWKAQIIAWoKEAoQCRADEAQQBQ==");
    case Kind::Video:    return QStringLiteral("EgWKAQIQAWoKEAoQCRADEAQQBQ==");
    case Kind::Playlist: return QStringLiteral("EgeKAQQoAEABagoQChAJEAMQBBAF");   // community playlists
    case Kind::Artist:   return QStringLiteral("EgWKAQIgAWoKEAoQCRADEAQQBQ==");
    }
    return {};
}

// Every object under `key`, wherever it sits in the answer, in order.
void collect(const QJsonValue &value, const QString &key, QVector<QJsonObject> &out)
{
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.key() == key && it.value().isObject())
                out << it.value().toObject();
            else
                collect(it.value(), key, out);
        }
    } else if (value.isArray()) {
        for (const QJsonValue &v : value.toArray())
            collect(v, key, out);
    }
}

QVector<QJsonObject> all(const QJsonValue &value, const QString &key)
{
    QVector<QJsonObject> out;
    collect(value, key, out);
    return out;
}

QJsonObject first(const QJsonValue &value, const QString &key)
{
    const QVector<QJsonObject> found = all(value, key);
    return found.isEmpty() ? QJsonObject() : found.first();
}

QString runs(const QJsonValue &text)
{
    QString out;
    for (const QJsonValue &run : text.toObject().value("runs").toArray())
        out += run.toObject().value("text").toString();
    if (out.isEmpty())
        out = text.toObject().value("simpleText").toString();
    return out;
}

// The smallest picture under a thumbnail object that is still sharp in a
// row (120 px or more), else the largest there is. The largest were 1000 px
// squares, scaled down at every frame of a scroll: the search page stuttered.
QString picture(const QJsonValue &thumbnail)
{
    QString url, largest;
    int best = 0, most = 0;
    const std::function<void(const QJsonValue &)> walk = [&](const QJsonValue &v) {
        if (v.isObject()) {
            const QJsonObject o = v.toObject();
            if (o.value("url").isString()) {
                const int w = o.value("width").toInt();
                if (w >= 120 && (best == 0 || w < best)) {
                    best = w;
                    url = o.value("url").toString();
                }
                if (w >= most) {
                    most = w;
                    largest = o.value("url").toString();
                }
            }
            for (const QJsonValue &child : o)
                walk(child);
        } else if (v.isArray()) {
            for (const QJsonValue &child : v.toArray())
                walk(child);
        }
    };
    walk(thumbnail);
    return url.isEmpty() ? largest : url;
}

bool isDuration(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^\\d{1,2}(:\\d{2}){1,2}$"));
    return re.match(s).hasMatch();
}

QStringList parts(const QString &line)
{
    QStringList out;
    for (const QString &p : line.split(QStringLiteral(" • ")))
        if (!p.trimmed().isEmpty())
            out << p.trimmed();
    // The unfiltered search puts the kind first ("Song • Duman • ...").
    static const QStringList kinds{"Song", "Video", "Playlist", "Artist", "Album", "Single", "EP", "Episode"};
    if (out.size() > 1 && kinds.contains(out.first()))
        out.removeFirst();
    return out;
}

// One row of a search shelf or a playlist page.
Item fromListItem(const QJsonObject &row, Kind kind)
{
    Item item;
    item.kind = kind;
    QStringList columns;
    for (const QJsonValue &c : row.value("flexColumns").toArray())
        columns << runs(c.toObject().value("musicResponsiveListItemFlexColumnRenderer").toObject().value("text"));
    for (const QJsonValue &c : row.value("fixedColumns").toArray()) {
        const QString fixed = runs(c.toObject().value("musicResponsiveListItemFixedColumnRenderer").toObject().value("text"));
        if (isDuration(fixed))
            item.duration = fixed;
    }
    item.title = columns.value(0);
    item.thumbnail = picture(row.value("thumbnail"));

    item.id = row.value("playlistItemData").toObject().value("videoId").toString();
    if (item.id.isEmpty())
        item.id = first(row.value("overlay"), "watchEndpoint").value("videoId").toString();
    if (kind == Kind::Playlist || kind == Kind::Artist) {
        QString browse = row.value("navigationEndpoint").toObject().value("browseEndpoint").toObject().value("browseId").toString();
        if (browse.startsWith(QLatin1String("VL")))
            browse = browse.mid(2);
        item.id = browse;
    }

    QStringList line = parts(columns.value(1));
    for (int i = line.size() - 1; i >= 0; --i)
        if (isDuration(line[i])) {
            if (item.duration.isEmpty())
                item.duration = line[i];
            line.removeAt(i);
        }
    switch (kind) {
    case Kind::Song:
        item.artist = line.value(0);
        // A search row is "artist • album"; a playlist page has the album
        // in a column of its own.
        item.album = line.size() > 1 ? line.value(1) : columns.value(2);
        if (columns.size() > 2 && columns.value(2).contains(QLatin1String("plays")))
            item.extra = columns.value(2);
        if (item.album == item.extra)
            item.album.clear();
        break;
    case Kind::Video:
        item.artist = line.value(0);
        item.extra = line.value(1);
        break;
    case Kind::Playlist:
    case Kind::Artist:
        item.artist = line.value(0);
        item.extra = line.size() > 1 ? line.last() : QString();
        break;
    }
    return item;
}

} // namespace

QString Item::lineTitle() const
{
    if (kind == Kind::Video || artist.isEmpty())
        return title;
    return artist + QStringLiteral(" - ") + title;
}

QString Item::target() const
{
    return QStringLiteral("https://www.youtube.com/watch?v=") + id;
}

QString playlistIdIn(const QString &text)
{
    const QUrl url(text.trimmed());
    if (!url.isValid() || !url.host().contains(QLatin1String("youtu")))
        return {};
    const QString list = QUrlQuery(url).queryItemValue("list");
    // Mixes ("RD..." made from a song, a channel or "My mix") are made per
    // viewer and cannot be read as a list; YouTube Music's own playlists
    // ("RDCLAK...") also start with RD and can.
    const bool mix = list.startsWith(QLatin1String("RD")) && !list.startsWith(QLatin1String("RDCLAK"));
    return mix ? QString() : list;
}

QString videoIdIn(const QString &text)
{
    const QUrl url(text.trimmed());
    if (!url.isValid() || !url.host().contains(QLatin1String("youtu")))
        return {};
    QString id = QUrlQuery(url).queryItemValue("v");
    if (id.isEmpty() && url.host().contains(QLatin1String("youtu.be")))
        id = url.path().mid(1);
    if (id.isEmpty() && url.path().startsWith(QLatin1String("/shorts/")))
        id = url.path().mid(8);
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{11}$"));
    return valid.match(id).hasMatch() ? id : QString();
}

Client::Client(QObject *parent) : QObject(parent) {}

void Client::post(const QString &endpoint, const QJsonObject &body, std::function<void(const QJsonObject &)> done)
{
    QNetworkRequest request(QUrl(QStringLiteral("https://music.youtube.com/youtubei/v1/%1?prettyPrint=false").arg(endpoint)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Origin", "https://music.youtube.com");
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0");
    request.setTransferTimeout(15000);
    QJsonObject client{{"clientName", "WEB_REMIX"}, {"clientVersion", "1.20241023.01.00"}, {"hl", "en"}};
    if (!m_region.isEmpty())
        client.insert("gl", m_region);
    QJsonObject full = body;
    full.insert("context", QJsonObject{{"client", client}});
    QNetworkReply *reply = m_net.post(request, QJsonDocument(full).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        done(reply->error() == QNetworkReply::NoError ? QJsonDocument::fromJson(reply->readAll()).object() : QJsonObject());
    });
}

void Client::search(const QString &query, Kind kind, std::function<void(QVector<Item>)> done)
{
    if (kind == Kind::Song)
        ++m_searchSerial;   // songs are asked first: one search, one serial
    const int serial = m_searchSerial;
    post("search", {{"query", query}, {"params", filterFor(kind)}}, [this, kind, serial, done](const QJsonObject &answer) {
        if (serial != m_searchSerial)
            return;
        QVector<Item> items;
        for (const QJsonObject &row : all(answer, "musicResponsiveListItemRenderer")) {
            Item item = fromListItem(row, kind);
            if (!item.id.isEmpty() && !item.title.isEmpty())
                items << item;
        }
        done(items);
    });
}

void Client::radio(const QString &videoId, std::function<void(QVector<Item>)> done)
{
    post("next", {{"videoId", videoId}, {"playlistId", QStringLiteral("RDAMVM") + videoId}, {"isAudioOnly", true}},
         [done](const QJsonObject &answer) {
        QVector<Item> items;
        for (const QJsonObject &row : all(answer, "playlistPanelVideoRenderer")) {
            Item item;
            item.kind = Kind::Song;
            item.id = row.value("videoId").toString();
            item.title = runs(row.value("title"));
            const QStringList line = parts(runs(row.value("longBylineText")));
            item.artist = line.value(0);
            item.duration = runs(row.value("lengthText"));
            item.thumbnail = picture(row.value("thumbnail"));
            if (!item.id.isEmpty() && !item.title.isEmpty())
                items << item;
        }
        done(items);
    });
}

void Client::playlist(const QString &id, std::function<void(PlaylistPage)> done)
{
    post("browse", {{"browseId", QStringLiteral("VL") + id}}, [this, id, done](const QJsonObject &answer) {
        PlaylistPage page;
        page.id = id;
        QJsonObject header = first(answer, "musicResponsiveHeaderRenderer");
        if (header.isEmpty())
            header = first(answer, "musicDetailHeaderRenderer");
        page.title = runs(header.value("title"));
        page.subtitle = runs(header.value("secondSubtitle"));
        page.thumbnail = picture(header.value("thumbnail"));
        for (const QJsonObject &row : all(answer, "musicResponsiveListItemRenderer")) {
            Item item = fromListItem(row, Kind::Song);
            if (!item.id.isEmpty() && !item.title.isEmpty())
                page.songs << item;
        }
        page.ok = !page.songs.isEmpty();
        QString token = first(answer, "nextContinuationData").value("continuation").toString();
        if (token.isEmpty())
            token = first(answer, "continuationCommand").value("token").toString();
        if (token.isEmpty())
            done(page);
        else
            morePages(page, token, 1, done);
    });
}

void Client::morePages(PlaylistPage page, const QString &token, int pages, std::function<void(PlaylistPage)> done)
{
    // 100 songs a page; 40 pages is far past any list worth importing.
    if (pages > 40) {
        done(page);
        return;
    }
    post("browse", {{"continuation", token}}, [this, page, pages, done](const QJsonObject &answer) mutable {
        int added = 0;
        for (const QJsonObject &row : all(answer, "musicResponsiveListItemRenderer")) {
            Item item = fromListItem(row, Kind::Song);
            if (!item.id.isEmpty() && !item.title.isEmpty()) {
                page.songs << item;
                ++added;
            }
        }
        QString next = first(answer, "nextContinuationData").value("continuation").toString();
        if (next.isEmpty())
            next = first(answer, "continuationCommand").value("token").toString();
        if (added == 0 || next.isEmpty())
            done(page);
        else
            morePages(page, next, pages + 1, done);
    });
}

} // namespace ytmusic
