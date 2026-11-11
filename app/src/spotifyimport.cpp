#include "spotifyimport.h"

#include <QDialogButtonBox>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace spotify {

namespace {
constexpr int kParallel = 4;      // measured: 170 track pages in 24 s, no 429
constexpr int kAttempts = 4;

const QString kPrivate = QStringLiteral(
    "This playlist is private. In Spotify, open it, press Ctrl+A then Ctrl+C, and paste here.");

// The __NEXT_DATA__ of an embed page, its entity, or nothing.
QJsonObject entityOf(const QByteArray &html, bool *notFound)
{
    static const QRegularExpression next(QStringLiteral(R"(<script id="__NEXT_DATA__" type="application/json">(.+?)</script>)"),
                                         QRegularExpression::DotMatchesEverythingOption);
    const auto m = next.match(QString::fromUtf8(html));
    if (!m.hasMatch())
        return {};
    const QJsonObject props = QJsonDocument::fromJson(m.captured(1).toUtf8()).object()
                                  .value("props").toObject().value("pageProps").toObject();
    if (notFound)
        *notFound = props.value("status").toInt() == 404;
    return props.value("state").toObject().value("data").toObject().value("entity").toObject();
}

// Spotify joins artists with a comma and a no-break space.
QString clean(QString s)
{
    return s.replace(QChar(0x00A0), QLatin1Char(' ')).trimmed();
}

QNetworkRequest embed(const QString &path)
{
    QNetworkRequest request(QUrl(QStringLiteral("https://open.spotify.com/embed/") + path));
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0");
    request.setRawHeader("Accept", "text/html,application/xhtml+xml");
    request.setTransferTimeout(15000);
    return request;
}
} // namespace

Request parse(const QString &text)
{
    Request r;
    static const QRegularExpression track(QStringLiteral(R"(track[/:]([A-Za-z0-9]{22}))"));
    QSet<QString> seen;
    for (auto it = track.globalMatch(text); it.hasNext();) {
        const QString id = it.next().captured(1);
        if (!seen.contains(id)) {
            seen.insert(id);
            r.trackIds << id;
        }
    }
    if (!r.trackIds.isEmpty()) {
        r.kind = Request::Tracks;
        return r;
    }
    static const QRegularExpression playlist(QStringLiteral(R"(playlist[/:]([A-Za-z0-9]+))"));
    static const QRegularExpression album(QStringLiteral(R"(album[/:]([A-Za-z0-9]+))"));
    if (const auto m = playlist.match(text); m.hasMatch()) {
        r.kind = Request::Playlist;
        r.id = m.captured(1);
        r.privateLink = text.contains(QStringLiteral("pt="));
    } else if (const auto a = album.match(text); a.hasMatch()) {
        r.kind = Request::Album;
        r.id = a.captured(1);
    }
    return r;
}

Importer::Importer(QObject *parent) : QObject(parent) {}

void Importer::cancel()
{
    ++m_serial;
    m_running = 0;
}

void Importer::start(const Request &request)
{
    ++m_serial;
    m_request = request;
    m_tracks.clear();
    m_next = m_running = m_done = 0;
    if (request.kind == Request::Tracks) {
        m_tracks.resize(request.trackIds.size());
        emit progress(0, int(m_tracks.size()));
        pump();
    } else {
        fetchList();
    }
}

void Importer::fetchList()
{
    if (m_request.privateLink) {
        emit finished({}, {}, 0, kPrivate);
        return;
    }
    const QString path = (m_request.kind == Request::Album ? QStringLiteral("album/") : QStringLiteral("playlist/")) + m_request.id;
    const int serial = m_serial;
    QNetworkReply *reply = m_net.get(embed(path));
    connect(reply, &QNetworkReply::finished, this, [this, reply, serial] {
        reply->deleteLater();
        if (serial != m_serial)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool notFound = status == 404;
        const QJsonObject entity = entityOf(reply->readAll(), &notFound);
        if (notFound) {
            emit finished({}, {}, 0, m_request.kind == Request::Playlist ? kPrivate
                                     : QStringLiteral("Spotify has no album at that link."));
            return;
        }
        if (reply->error() != QNetworkReply::NoError && entity.isEmpty()) {
            emit finished({}, {}, 0, QStringLiteral("Spotify didn't answer: %1").arg(reply->errorString()));
            return;
        }
        QVector<Track> tracks;
        for (const QJsonValue &v : entity.value("trackList").toArray()) {
            const QJsonObject t = v.toObject();
            const QString title = clean(t.value("title").toString());
            if (!title.isEmpty())
                tracks << Track{clean(t.value("subtitle").toString()), title};
        }
        if (tracks.isEmpty()) {
            emit finished({}, {}, 0, QStringLiteral("That list came back empty."));
            return;
        }
        emit progress(int(tracks.size()), int(tracks.size()));
        emit finished(clean(entity.value("name").toString()), tracks, 0, {});
    });
}

void Importer::pump()
{
    while (m_running < kParallel && m_next < m_tracks.size()) {
        ++m_running;
        fetchTrack(m_next++, 0);
    }
    if (m_running == 0 && m_done >= m_tracks.size()) {
        QVector<Track> read;
        int unread = 0;
        for (const Track &t : m_tracks) {
            if (t.title.isEmpty())
                ++unread;
            else
                read << t;
        }
        emit finished({}, read, unread, read.isEmpty() ? QStringLiteral("None of those songs could be read.") : QString());
    }
}

void Importer::fetchTrack(int index, int attempt)
{
    const int serial = m_serial;
    QNetworkReply *reply = m_net.get(embed(QStringLiteral("track/") + m_request.trackIds[index]));
    connect(reply, &QNetworkReply::finished, this, [this, reply, serial, index, attempt] {
        reply->deleteLater();
        if (serial != m_serial)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // Too many at once (429), or the server or the line hiccuped: back
        // off and try again, longer each time (Retry-After when given).
        if ((status == 429 || status >= 500 || status == 0) && attempt + 1 < kAttempts) {
            int wait = 1500 * (1 << attempt);
            const int after = reply->rawHeader("Retry-After").toInt();
            if (after > 0)
                wait = qMin(after, 30) * 1000;
            QTimer::singleShot(wait, this, [this, serial, index, attempt] {
                if (serial == m_serial)
                    fetchTrack(index, attempt + 1);
            });
            return;
        }
        const QJsonObject entity = entityOf(reply->readAll(), nullptr);
        QStringList artists;
        for (const QJsonValue &a : entity.value("artists").toArray())
            artists << clean(a.toObject().value("name").toString());
        const QString title = clean(entity.value("name").toString());
        if (!title.isEmpty())
            m_tracks[index] = Track{artists.join(QStringLiteral(", ")), title};
        --m_running;
        ++m_done;
        emit progress(m_done, int(m_tracks.size()));
        pump();
    });
}

} // namespace spotify

SpotifyImportDialog::SpotifyImportDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Import from Spotify");
    resize(560, 420);
    auto *column = new QVBoxLayout(this);
    auto *about = new QLabel(
        "Paste a Spotify playlist or album link (Spotify shows up to 100 songs of it), or songs copied in the "
        "Spotify app: open the list, press Ctrl+A then Ctrl+C, and paste here. That way private and long lists "
        "come whole.");
    about->setWordWrap(true);
    column->addWidget(about);
    m_text = new QPlainTextEdit;
    m_text->setPlaceholderText("https://open.spotify.com/playlist/...\nor\nhttps://open.spotify.com/track/...\nhttps://open.spotify.com/track/...");
    column->addWidget(m_text, 1);
    m_bar = new QProgressBar;
    m_bar->setTextVisible(false);
    m_bar->hide();
    column->addWidget(m_bar);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    column->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_import = buttons->addButton("Import", QDialogButtonBox::AcceptRole);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, [this] {
        m_importer.cancel();
        reject();
    });
    connect(m_import, &QPushButton::clicked, this, &SpotifyImportDialog::begin);

    connect(&m_importer, &spotify::Importer::progress, this, [this](int done, int total) {
        m_bar->setRange(0, qMax(1, total));
        m_bar->setValue(done);
        m_status->setText(QStringLiteral("%1 / %2").arg(done).arg(total));
    });
    connect(&m_importer, &spotify::Importer::finished, this,
            [this](QString name, const QVector<spotify::Track> &tracks, int unread, const QString &error) {
        m_import->setEnabled(true);
        m_text->setEnabled(true);
        if (!error.isEmpty()) {
            m_bar->hide();
            m_status->setText(error);
            return;
        }
        if (name.isEmpty()) {
            bool ok = false;
            name = QInputDialog::getText(this, "Import from Spotify",
                                         QStringLiteral("%1 songs read%2. Name the playlist:")
                                             .arg(tracks.size())
                                             .arg(unread ? QStringLiteral(" (%1 couldn't be read)").arg(unread) : QString()),
                                         QLineEdit::Normal, "Spotify import", &ok).trimmed();
            if (!ok || name.isEmpty()) {
                m_status->setText("Not saved.");
                return;
            }
        }
        if (onImported)
            onImported(name, tracks);
        accept();
    });
}

void SpotifyImportDialog::begin()
{
    const spotify::Request request = spotify::parse(m_text->toPlainText());
    if (request.kind == spotify::Request::Nothing) {
        m_status->setText("That isn't a Spotify playlist, album or song link.");
        return;
    }
    m_import->setEnabled(false);
    m_text->setEnabled(false);
    m_bar->show();
    m_bar->setRange(0, 0);
    m_status->setText(request.kind == spotify::Request::Tracks ? QStringLiteral("0 / %1").arg(request.trackIds.size())
                                                               : QStringLiteral("Reading the list..."));
    m_importer.start(request);
}
