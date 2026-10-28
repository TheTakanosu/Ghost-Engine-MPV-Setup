#include "lyrics.h"

#include "theme.h"

#include <QClipboard>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QFileDialog>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QDesktopServices>
#include <QPushButton>
#include <QUrl>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrlQuery>
#include <QWheelEvent>
#include <cmath>

namespace {

constexpr int kRetries = 3;              // LRCLIB answers 503 when busy
constexpr qint64 kMissKeptDays = 3;      // a miss is asked about again after this
constexpr double kSyncSlack = 8;         // seconds a timed version may differ by
const char *kAgent = "GhostEngine/" GHOST_VERSION " (https://github.com/TheTakanosu/Ghost-Engine-MPV-Setup)";

// "[01:23.45] words" lines; anything else (tags, blank stamps) is skipped.
QVector<Lyrics::Line> parseLrc(const QString &lrc)
{
    static const QRegularExpression stamp(QStringLiteral(R"(^\[(\d+):(\d+(?:\.\d+)?)\](.*)$)"));
    QVector<Lyrics::Line> lines;
    for (const QString &raw : lrc.split('\n')) {
        const auto m = stamp.match(raw.trimmed());
        if (!m.hasMatch())
            continue;
        lines.append({m.captured(1).toInt() * 60 + m.captured(2).toDouble(), m.captured(3).trimmed()});
    }
    return lines;
}

QFont lineFont(bool current)
{
    QFont f(QStringLiteral("Segoe UI"));
    f.setPointSizeF(22 * theme::zoom());
    f.setWeight(current ? QFont::Black : QFont::Bold);
    return f;
}

} // namespace

// -- LRCLIB ----------------------------------------------------------------------

LyricsSource::LyricsSource(QObject *parent) : QObject(parent)
{
    m_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/lyrics");
    QDir().mkpath(m_dir);
}

QString LyricsSource::keyFor(const QString &artist, const QString &track)
{
    return (artist.trimmed() + QStringLiteral(" | ") + track.trimmed()).toLower();
}

QString LyricsSource::cacheFile(const QString &key) const
{
    return m_dir + '/' + QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex() + ".json";
}

void LyricsSource::fetch(const QString &artist, const QString &track, double duration)
{
    const QString key = keyFor(artist, track);
    QFile cached(cacheFile(key));
    if (cached.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(cached.readAll()).object();
        const bool miss = o.value("miss").toBool();
        const QDateTime when = QDateTime::fromString(o.value("at").toString(), Qt::ISODate);
        if (!miss || when.daysTo(QDateTime::currentDateTime()) < kMissKeptDays) {
            emit ready(key, miss ? Lyrics{} : pick(QJsonDocument(o.value("results").toArray()).toJson(), duration, key));
            return;
        }
    }
    QUrlQuery q;
    q.addQueryItem("track_name", track);
    if (!artist.isEmpty())
        q.addQueryItem("artist_name", artist);
    ask(key, q.toString(QUrl::FullyEncoded), duration, 0, false);
}

void LyricsSource::ask(const QString &key, const QString &query, double duration, int attempt, bool broad)
{
    QNetworkRequest request(QUrl(QStringLiteral("https://lrclib.net/api/search?") + query));
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kAgent));
    request.setTransferTimeout(10000);
    QNetworkReply *reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, [=] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if ((status == 503 || reply->error() != QNetworkReply::NoError) && attempt + 1 < kRetries) {
            QTimer::singleShot(2000 * (attempt + 1), this, [=] { ask(key, query, duration, attempt + 1, broad); });
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit ready(key, Lyrics{});   // offline: not remembered, asked again next time
            return;
        }
        Lyrics lyrics = pick(body, duration, key);
        if (!lyrics.found && !broad) {
            // Titles from YouTube rarely split as LRCLIB has them; one free
            // search over everything comes next.
            const QStringList parts = key.split(QStringLiteral(" | "));
            QUrlQuery q;
            q.addQueryItem("q", (parts.value(0) + ' ' + parts.value(1)).trimmed());
            ask(key, q.toString(QUrl::FullyEncoded), duration, 0, true);
            return;
        }
        remember(key, lyrics, body);
        emit ready(key, lyrics);
    });
}

Lyrics LyricsSource::pick(const QByteArray &json, double duration, const QString &key) const
{
    // Names compared as letters and digits only, case and accents aside:
    // "BEGE" is "bege", "Işıklarım Ol" is "isiklarim ol".
    auto plain = [](QString s) {
        s = s.toLower().replace(QChar(0x0131), 'i').normalized(QString::NormalizationForm_D);
        QString out;
        for (const QChar c : s)
            if (c.isLetterOrNumber())
                out += c;
        return out;
    };
    const QStringList parts = key.split(QStringLiteral(" | "));
    const QString artist = plain(parts.value(0));
    const QString track = plain(parts.value(1));
    auto belongs = [&](const QJsonObject &o) {
        const QString a = plain(o.value("artistName").toString());
        const QString t = plain(o.value("trackName").toString());
        if (!artist.isEmpty())
            return a.contains(artist) || artist.contains(a) || t.contains(artist);
        // No artist known: only the very same title.
        return !track.isEmpty() && t == track;
    };
    // Timed lyrics of a version the same length first, then any lyrics. A
    // timed version of another length (slowed, sped up, live) is shown as
    // plain text: its times would be wrong.
    const QJsonArray results = QJsonDocument::fromJson(json).array();
    Lyrics best;
    double bestScore = -1;
    for (const QJsonValue &v : results) {
        const QJsonObject o = v.toObject();
        if (o.value("instrumental").toBool() || !belongs(o))
            continue;
        const QString synced = o.value("syncedLyrics").toString();
        const QString plain = o.value("plainLyrics").toString();
        if (synced.isEmpty() && plain.isEmpty())
            continue;
        const double gap = duration > 0 ? std::abs(o.value("duration").toDouble() - duration) : 0;
        const bool timed = !synced.isEmpty() && gap <= kSyncSlack;
        const double score = (timed ? 1000 : 0) - qMin(gap, 999.0);
        if (score <= bestScore)
            continue;
        bestScore = score;
        best = Lyrics{};
        best.found = true;
        best.plain = plain.isEmpty() ? synced : plain;
        if (timed)
            best.lines = parseLrc(synced);
        if (best.plain.contains('['))   // plain text made from timed lines
            best.plain.remove(QRegularExpression(QStringLiteral(R"(\[\d+:\d+(?:\.\d+)?\]\s*)")));
    }
    return best;
}

void LyricsSource::remember(const QString &key, const Lyrics &lyrics, const QByteArray &raw)
{
    QJsonObject o;
    o["key"] = key;
    o["at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    o["miss"] = !lyrics.found;
    if (lyrics.found)
        o["results"] = QJsonDocument::fromJson(raw).array();
    QFile file(cacheFile(key));
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// -- the page ----------------------------------------------------------------------

LyricsView::LyricsView(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    m_genius = new QPushButton("Search for the lyrics on Genius", this);
    m_genius->setObjectName("link");
    m_genius->setCursor(Qt::PointingHandCursor);
    m_genius->hide();
    // Out of the app: the words to the clipboard or to a file.
    m_copy = new QPushButton("Copy lyrics", this);
    m_save = new QPushButton("Save as...", this);
    for (QPushButton *b : {m_copy, m_save}) {
        b->setObjectName("chip");
        b->setCursor(Qt::PointingHandCursor);
        b->hide();
    }
    connect(m_copy, &QPushButton::clicked, this, &LyricsView::copyLyrics);
    connect(m_save, &QPushButton::clicked, this, &LyricsView::saveLyrics);
    setFocusPolicy(Qt::ClickFocus);
    connect(m_genius, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://genius.com/search?q=") +
                                       QString::fromUtf8(QUrl::toPercentEncoding(m_searchText))));
    });
    m_glide.setInterval(16);
    connect(&m_glide, &QTimer::timeout, this, [this] {
        const qreal step = (m_targetScroll - m_scroll) * 0.18;
        if (std::abs(step) < 0.3) {
            m_scroll = m_targetScroll;
            m_glide.stop();
        } else {
            m_scroll += step;
        }
        update();
    });
}

void LyricsView::setSearching(const QString &title)
{
    m_lyrics = Lyrics{};
    m_title = title;
    m_state = QStringLiteral("searching");
    m_genius->hide();
    m_copy->hide();
    m_save->hide();
    m_selFrom = m_selTo = -1;
    m_scroll = m_targetScroll = 0;
    update();
}

void LyricsView::setLyrics(const Lyrics &lyrics, const QString &title)
{
    m_lyrics = lyrics;
    m_title = title;
    m_state = lyrics.found ? QString() : QStringLiteral("missing");
    m_genius->setVisible(!lyrics.found && !m_searchText.isEmpty());
    m_copy->setVisible(lyrics.found);
    m_save->setVisible(lyrics.found);
    m_selFrom = m_selTo = -1;
    resizeEvent(nullptr);
    m_scroll = m_targetScroll = 0;
    m_userScrolledAt = 0;
    followCurrent();
    update();
}

void LyricsView::setSearchText(const QString &text)
{
    m_searchText = text;
}

void LyricsView::resizeEvent(QResizeEvent *)
{
    m_genius->adjustSize();
    m_genius->move((width() - m_genius->width()) / 2, height() / 2 + 60);
    m_save->adjustSize();
    m_copy->adjustSize();
    m_save->move(width() - m_save->width() - 24, height() - m_save->height() - 18);
    m_copy->move(m_save->x() - m_copy->width() - 8, m_save->y());
}

void LyricsView::setTint(const QColor &tint)
{
    m_tint = tint;
    update();
}

void LyricsView::setPosition(double seconds)
{
    const int before = currentLine();
    m_position = seconds;
    if (currentLine() != before) {
        followCurrent();
        update();
    }
}

int LyricsView::currentLine() const
{
    int at = -1;
    for (int i = 0; i < m_lyrics.lines.size() && m_lyrics.lines[i].at <= m_position + 0.15; ++i)
        at = i;
    return at;
}

QVector<QRectF> LyricsView::layoutLines(qreal width) const
{
    // Each line wrapped to the width, one under the other.
    QVector<QRectF> rects;
    const QStringList plain = m_lyrics.lines.isEmpty() ? m_lyrics.plain.split('\n') : QStringList();
    const int count = m_lyrics.lines.isEmpty() ? int(plain.size()) : int(m_lyrics.lines.size());
    const QFontMetricsF fm(lineFont(true));
    qreal y = 0;
    for (int i = 0; i < count; ++i) {
        const QString text = m_lyrics.lines.isEmpty() ? plain[i] : m_lyrics.lines[i].text;
        const QRectF r = fm.boundingRect(QRectF(0, 0, width, 1e6), Qt::TextWordWrap, text.isEmpty() ? QStringLiteral("♪") : text);
        rects.append(QRectF(0, y, width, r.height()));
        y += r.height() + 14 * theme::zoom();
    }
    return rects;
}

void LyricsView::followCurrent()
{
    // The line being sung a third of the way down, unless the wheel moved
    // the page a moment ago.
    if (QDateTime::currentMSecsSinceEpoch() - m_userScrolledAt < 4000)
        return;
    const int line = currentLine();
    if (line < 0) {
        m_targetScroll = 0;
    } else {
        const QVector<QRectF> rects = layoutLines(textArea().width());
        m_targetScroll = qMax(0.0, rects.value(line).center().y() - height() / 3.0);
    }
    if (!m_glide.isActive())
        m_glide.start();
}

void LyricsView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor base = m_tint.isValid() ? m_tint : QColor(70, 52, 102);
    QLinearGradient wash(0, 0, 0, height());
    wash.setColorAt(0, base);
    wash.setColorAt(1, base.darker(190));
    QPainterPath box;
    box.addRoundedRect(QRectF(rect()), 12, 12);
    p.fillPath(box, wash);

    const QRectF area = textArea();
    if (!m_state.isEmpty()) {
        QFont f = lineFont(true);
        p.setFont(f);
        p.setPen(Qt::white);
        const QString big = m_state == "searching" ? QStringLiteral("Looking for the words…")
                                                     : QStringLiteral("No lyrics for this song yet");
        p.drawText(area.adjusted(0, 0, 0, -area.height() / 2), Qt::AlignHCenter | Qt::AlignBottom, big);
        f.setPointSizeF(11 * theme::zoom());
        f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, 170));
        p.drawText(area.adjusted(0, area.height() / 2 + 12, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
                   m_state == "searching" ? m_title
                                          : QStringLiteral("Lyrics come from LRCLIB, a free database anyone can add to."));
        return;
    }

    const QVector<QRectF> rects = layoutLines(area.width());
    const QStringList plain = m_lyrics.lines.isEmpty() ? m_lyrics.plain.split('\n') : QStringList();
    const int current = currentLine();
    const bool timed = !m_lyrics.lines.isEmpty();
    // The words go on a layer of their own whose top and bottom edges are
    // then made see-through, so lines fade into whatever colour is behind
    // them instead of being cut (a painted band never matched the wash).
    const qreal dpr = devicePixelRatioF();
    QImage layer((area.size() + QSizeF(40, 0)).toSize() * dpr, QImage::Format_ARGB32_Premultiplied);
    layer.setDevicePixelRatio(dpr);
    layer.fill(Qt::transparent);
    QPainter lp(&layer);
    lp.setRenderHint(QPainter::Antialiasing);
    lp.setRenderHint(QPainter::TextAntialiasing);
    lp.translate(20, -m_scroll);
    for (int i = 0; i < rects.size(); ++i) {
        if (rects[i].bottom() < m_scroll - 40 || rects[i].top() > m_scroll + area.height() + 40)
            continue;
        const QString text = timed ? m_lyrics.lines[i].text : plain[i];
        // Sung lines stay white but softer, lines to come are dim; untimed
        // lyrics are all one colour.
        QColor ink = Qt::white;
        if (timed)
            ink.setAlpha(i == current ? 255 : i < current ? 150 : 95);
        if (m_selFrom >= 0 && i >= qMin(m_selFrom, m_selTo) && i <= qMax(m_selFrom, m_selTo)) {
            const QFontMetricsF fm(lineFont(timed && i == current));
            const qreal w = qMin(rects[i].width(), fm.boundingRect(rects[i], Qt::TextWordWrap, text.isEmpty() ? QStringLiteral("♪") : text).width());
            QPainterPath mark;
            mark.addRoundedRect(QRectF(rects[i].left() - 8, rects[i].top() - 3, w + 16, rects[i].height() + 6), 6, 6);
            lp.fillPath(mark, QColor(255, 255, 255, 46));
            ink.setAlpha(255);
        }
        lp.setFont(lineFont(timed && i == current));
        lp.setPen(ink);
        lp.drawText(rects[i], Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, text.isEmpty() ? QStringLiteral("♪") : text);
    }
    const qreal fade = 70 * theme::zoom();
    lp.resetTransform();
    lp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    QLinearGradient mask(0, 0, 0, area.height());
    const qreal edge = qMin(0.45, fade / area.height());
    mask.setColorAt(0, Qt::transparent);
    mask.setColorAt(edge, Qt::black);
    mask.setColorAt(1 - edge, Qt::black);
    mask.setColorAt(1, Qt::transparent);
    lp.fillRect(QRectF(QPointF(0, 0), area.size() + QSizeF(40, 0)), mask);
    lp.end();
    p.drawImage(QPointF(area.left() - 20, area.top()), layer);
    if (!timed) {
        QFont f = lineFont(false);
        f.setPointSizeF(9 * theme::zoom());
        f.setWeight(QFont::Normal);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255, 150));
        p.drawText(QRectF(rect()).adjusted(60, 16, -60, 0), Qt::AlignLeft | Qt::AlignTop,
                   QStringLiteral("These lyrics aren't timed to this version of the song."));
    }
}

void LyricsView::wheelEvent(QWheelEvent *event)
{
    const QVector<QRectF> rects = layoutLines(textArea().width());
    const qreal end = rects.isEmpty() ? 0 : qMax(0.0, rects.last().bottom() - height() / 2.0);
    m_targetScroll = qBound(0.0, m_targetScroll - event->angleDelta().y() * 1.2, end);
    m_userScrolledAt = QDateTime::currentMSecsSinceEpoch();
    if (!m_glide.isActive())
        m_glide.start();
}

QRectF LyricsView::textArea() const
{
    const bool untimed = m_state.isEmpty() && m_lyrics.lines.isEmpty();
    return QRectF(rect()).adjusted(60, untimed ? 64 : 40, -60, -70);
}

int LyricsView::lineAt(const QPointF &pos) const
{
    const QRectF area = textArea();
    const QPointF at = pos - QPointF(area.left(), area.top() - m_scroll);
    const QVector<QRectF> rects = layoutLines(area.width());
    for (int i = 0; i < rects.size(); ++i)
        if (at.y() >= rects[i].top() - 7 && at.y() < rects[i].bottom() + 7)
            return i;
    return -1;
}

QStringList LyricsView::lineTexts() const
{
    if (m_lyrics.lines.isEmpty())
        return m_lyrics.plain.split('\n');
    QStringList out;
    for (const Lyrics::Line &l : m_lyrics.lines)
        out << l.text;
    return out;
}

QString LyricsView::selectedText() const
{
    const QStringList all = lineTexts();
    if (m_selFrom < 0)
        return all.join('\n');
    return all.mid(qMin(m_selFrom, m_selTo), qAbs(m_selTo - m_selFrom) + 1).join('\n');
}

void LyricsView::copyLyrics()
{
    QGuiApplication::clipboard()->setText(selectedText());
}

void LyricsView::saveLyrics()
{
    // Plain text, or with the times as an .lrc that players read.
    QString name = m_searchText.isEmpty() ? QStringLiteral("lyrics") : m_searchText;
    name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|]+)")), QStringLiteral(" "));
    const bool timed = !m_lyrics.lines.isEmpty();
    QString chosen;
    const QString target = QFileDialog::getSaveFileName(this, "Save lyrics",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + '/' + name.simplified() + ".txt",
        timed ? QStringLiteral("Text (*.txt);;Timed lyrics (*.lrc)") : QStringLiteral("Text (*.txt)"), &chosen);
    if (target.isEmpty())
        return;
    QString text;
    if (target.endsWith(QStringLiteral(".lrc"), Qt::CaseInsensitive) && timed) {
        for (const Lyrics::Line &l : m_lyrics.lines)
            text += QStringLiteral("[%1:%2]%3\n").arg(int(l.at) / 60, 2, 10, QChar('0'))
                        .arg(l.at - int(l.at) / 60 * 60, 5, 'f', 2, QChar('0')).arg(l.text);
    } else {
        text = lineTexts().join('\n') + '\n';
    }
    QFile file(target);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(text.toUtf8());
}

void LyricsView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_state.isEmpty())
        return;
    m_pressAt = event->position();
    m_pressLine = lineAt(event->position());
    m_selecting = false;
}

void LyricsView::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy)) {
        copyLyrics();
    } else if (event->matches(QKeySequence::SelectAll) && m_state.isEmpty()) {
        m_selFrom = 0;
        m_selTo = int(lineTexts().size()) - 1;
        update();
    } else if (event->key() == Qt::Key_Escape) {
        m_selFrom = m_selTo = -1;
        update();
    } else {
        QWidget::keyPressEvent(event);
    }
}

void LyricsView::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_state.isEmpty())
        return;
    QMenu menu(this);
    if (m_selFrom >= 0)
        menu.addAction("Copy selected lines\tCtrl+C", this, &LyricsView::copyLyrics);
    menu.addAction("Copy all lyrics", this, [this] {
        QGuiApplication::clipboard()->setText(lineTexts().join('\n'));
    });
    menu.addAction("Select all\tCtrl+A", this, [this] {
        m_selFrom = 0;
        m_selTo = int(lineTexts().size()) - 1;
        update();
    });
    menu.addSeparator();
    menu.addAction("Save lyrics as...", this, &LyricsView::saveLyrics);
    menu.exec(event->globalPos());
}

void LyricsView::mouseMoveEvent(QMouseEvent *event)
{
    // Dragging over lines selects them, as in a text box.
    if ((event->buttons() & Qt::LeftButton) && m_pressLine >= 0 &&
        (m_selecting || (event->position() - m_pressAt).manhattanLength() > 6)) {
        m_selecting = true;
        const int line = lineAt(event->position());
        m_selFrom = m_pressLine;
        if (line >= 0)
            m_selTo = line;
        else if (m_selTo < 0)
            m_selTo = m_pressLine;
        m_userScrolledAt = QDateTime::currentMSecsSinceEpoch();   // stay put while selecting
        update();
        return;
    }
    if (m_lyrics.lines.isEmpty()) {
        unsetCursor();
        return;
    }
    if (lineAt(event->position()) >= 0)
        setCursor(Qt::PointingHandCursor);
    else
        unsetCursor();
}

void LyricsView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_selecting) {
        m_selecting = false;
        return;
    }
    // A plain click clears a selection; on a timed line it also plays from
    // where that line starts.
    if (m_selFrom >= 0) {
        m_selFrom = m_selTo = -1;
        update();
    }
    if (m_lyrics.lines.isEmpty() || !onSeek)
        return;
    const int line = lineAt(event->position());
    if (line >= 0) {
        m_userScrolledAt = 0;
        onSeek(m_lyrics.lines[line].at);
    }
}
