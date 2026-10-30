#include "nowplayingpanel.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QVBoxLayout>

// -- YouTube Music's artist page ------------------------------------------------

ArtistLookup::ArtistLookup(QObject *parent) : QObject(parent) {}

const ArtistInfo *ArtistLookup::known(const QString &key) const
{
    const auto it = m_known.constFind(key);
    return it == m_known.constEnd() ? nullptr : &it.value();
}

static QString runsText(const QJsonValue &value)
{
    QString text;
    for (const QJsonValue &run : value.toObject().value("runs").toArray())
        text += run.toObject().value("text").toString();
    return text;
}

// The first object found under `key`, searching depth first.
static QJsonObject findKey(const QJsonValue &value, const QString &key)
{
    if (value.isObject()) {
        const QJsonObject o = value.toObject();
        if (o.contains(key))
            return o.value(key).toObject();
        for (const QJsonValue &v : o) {
            const QJsonObject found = findKey(v, key);
            if (!found.isEmpty())
                return found;
        }
    } else if (value.isArray()) {
        for (const QJsonValue &v : value.toArray()) {
            const QJsonObject found = findKey(v, key);
            if (!found.isEmpty())
                return found;
        }
    }
    return {};
}

void ArtistLookup::post(const QString &endpoint, const QJsonObject &body, std::function<void(const QJsonObject &)> done)
{
    QNetworkRequest request(QUrl(QStringLiteral("https://music.youtube.com/youtubei/v1/%1?prettyPrint=false").arg(endpoint)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Origin", "https://music.youtube.com");
    request.setHeader(QNetworkRequest::UserAgentHeader, "Mozilla/5.0");
    const QJsonObject client{{"clientName", "WEB_REMIX"}, {"clientVersion", "1.20241023.01.00"}, {"hl", "en"}};
    QJsonObject full = body;
    full.insert("context", QJsonObject{{"client", client}});
    QNetworkReply *reply = m_net.post(request, QJsonDocument(full).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        done(QJsonDocument::fromJson(reply->readAll()).object());
    });
}

void ArtistLookup::fetch(const QString &name, const QString &channelId)
{
    const QString key = keyFor(name.isEmpty() ? channelId : name);
    if (key.isEmpty() || m_known.contains(key) || m_asking.contains(key))
        return;
    m_asking.insert(key);
    if (name.isEmpty()) {
        fetchPage(key, channelId);
        return;
    }
    // A search for the name puts the artist's card on top when there is one.
    post("search", {{"query", name}}, [this, key, name, channelId](const QJsonObject &answer) {
        const QJsonObject card = findKey(answer, "musicCardShelfRenderer");
        const QJsonArray runs = card.value("title").toObject().value("runs").toArray();
        const QJsonObject title = runs.isEmpty() ? QJsonObject() : runs.at(0).toObject();
        const QJsonObject browse = title.value("navigationEndpoint").toObject().value("browseEndpoint").toObject();
        const QString type = browse.value("browseEndpointContextSupportedConfigs").toObject()
                                 .value("browseEndpointContextMusicConfig").toObject().value("pageType").toString();
        const bool artist = type == QLatin1String("MUSIC_PAGE_TYPE_ARTIST") &&
                            keyFor(title.value("text").toString()) == keyFor(name);
        fetchPage(key, artist ? browse.value("browseId").toString() : channelId);
    });
}

void ArtistLookup::fetchPage(const QString &key, const QString &browseId)
{
    if (browseId.isEmpty()) {
        m_known.insert(key, ArtistInfo());
        m_asking.remove(key);
        emit ready(key);
        return;
    }
    post("browse", {{"browseId", browseId}}, [this, key](const QJsonObject &answer) {
        const QJsonObject header = answer.value("header").toObject();
        // An artist's page has the immersive header (a wide photo); a small
        // artist's sometimes the visual one.
        QJsonObject h = header.value("musicImmersiveHeaderRenderer").toObject();
        if (h.isEmpty())
            h = header.value("musicVisualHeaderRenderer").toObject();
        ArtistInfo info;
        info.name = runsText(h.value("title"));
        info.audience = runsText(h.value("monthlyListenerCount"));
        if (info.audience.isEmpty()) {
            const QString subscribers = runsText(h.value("subscriptionButton").toObject().value("subscribeButtonRenderer")
                                                     .toObject().value("longSubscriberCountText"));
            info.audience = subscribers;
        }
        info.about = runsText(h.value("description")).trimmed();
        // YouTube Music ends a Wikipedia biography with "From Wikipedia
        // (link) under Creative Commons ... (link)": said once, without
        // the links, under the text instead.
        static const QRegularExpression credit(QStringLiteral(R"(\s*From Wikipedia\b.*$)"),
                                               QRegularExpression::DotMatchesEverythingOption);
        if (info.about.contains(credit)) {
            info.about.remove(credit);
            info.source = QStringLiteral("Wikipedia");
        }
        info.found = !info.name.isEmpty();
        // The photo: the first size at least 800 px wide.
        QString photo;
        const QJsonArray sizes = h.value("thumbnail").toObject().value("musicThumbnailRenderer").toObject()
                                     .value("thumbnail").toObject().value("thumbnails").toArray();
        for (const QJsonValue &s : sizes) {
            photo = s.toObject().value("url").toString();
            if (s.toObject().value("width").toInt() >= 800)
                break;
        }
        m_known.insert(key, info);
        m_asking.remove(key);
        emit ready(key);
        if (photo.isEmpty())
            return;
        QNetworkReply *image = m_net.get(QNetworkRequest(QUrl(photo)));
        connect(image, &QNetworkReply::finished, this, [this, image, key] {
            image->deleteLater();
            QPixmap picture;
            if (picture.loadFromData(image->readAll()) && m_known.contains(key)) {
                m_known[key].photo = picture;
                emit ready(key);
            }
        });
    });
}

// -- pieces of the panel ---------------------------------------------------------

namespace {

QColor softTint(const QPixmap &cover)
{
    if (cover.isNull())
        return QColor(46, 52, 120);
    const QColor avg = cover.toImage().scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixelColor(0, 0);
    return QColor::fromHsv(avg.hsvHue(), qBound(90, avg.hsvSaturation(), 190), qBound(70, avg.value(), 120));
}

} // namespace

// The cover, square and as wide as the panel, its middle taken from the
// video's 16:9 picture.
class SquareCover : public QWidget {
public:
    QPixmap picture;
    SquareCover() { setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return w; }
    QSize sizeHint() const override { return {300, 300}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF box(0, 0, width(), width());
        QPainterPath shape;
        shape.addRoundedRect(box, 8, 8);
        p.setClipPath(shape);
        if (picture.isNull()) {
            p.fillRect(box, QColor(40, 38, 48));
            return;
        }
        const int side = qMin(picture.width(), picture.height());
        p.drawPixmap(box, picture, QRectF((picture.width() - side) / 2.0, (picture.height() - side) / 2.0, side, side));
    }
};

class ClickCard : public QFrame {
public:
    std::function<void()> clicked;
    ClickCard()
    {
        setObjectName("npCard");
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && clicked)
            clicked();
    }
};

// The artist's photo, wide, with "About the artist" written over it as
// Spotify does.
class ArtistPhoto : public QWidget {
public:
    QPixmap picture;
    ArtistPhoto() { setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override { return picture.isNull() ? 44 : int(w * 0.62); }
    QSize sizeHint() const override { return {300, heightForWidth(300)}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF box = rect();
        QPainterPath shape;
        shape.addRoundedRect(box.adjusted(0, 0, 0, 10), 10, 10);   // round at the top only
        p.setClipPath(shape);
        if (!picture.isNull()) {
            // Filled, cut from the middle.
            const qreal scale = qMax(box.width() / picture.width(), box.height() / picture.height());
            const QSizeF drawn(picture.width() * scale, picture.height() * scale);
            p.drawPixmap(QRectF(QPointF((box.width() - drawn.width()) / 2, (box.height() - drawn.height()) / 2), drawn),
                         picture, picture.rect());
            QLinearGradient shade(0, 0, 0, 60);
            shade.setColorAt(0, QColor(0, 0, 0, 150));
            shade.setColorAt(1, QColor(0, 0, 0, 0));
            p.fillRect(QRectF(0, 0, box.width(), 60), shade);
        }
        QFont font = p.font();
        font.setPointSizeF(10.5);
        font.setWeight(QFont::Bold);
        p.setFont(font);
        p.setPen(Qt::white);
        p.drawText(box.adjusted(14, 12, -14, 0), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("About the artist"));
    }
};

// -- the panel --------------------------------------------------------------------

static QLabel *heading(const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet("font-size: 10.5pt; font-weight: 700; color: rgb(240,240,245); background: transparent;");
    return label;
}

NowPlayingPanel::NowPlayingPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("panel");
    setAttribute(Qt::WA_StyledBackground);
    setMinimumWidth(280);
    setStyleSheet(
        "QFrame#npCard { background: rgb(36,34,44); border-radius: 10px; }"
        "QLabel { background: transparent; }"
        "QPushButton#npSwitch { background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.14);"
        "  border-radius: 16px; min-height: 30px; max-height: 30px; padding: 0 16px; color: rgb(240,240,245);"
        "  font-weight: 600; }"
        "QPushButton#npSwitch:hover { background: rgba(255,255,255,0.16); border-color: rgba(255,255,255,0.3); }"
        "QPushButton#npSwitch:disabled { color: rgb(154,152,166); }");

    m_list = new QLabel;
    m_list->setStyleSheet("font-size: 11pt; font-weight: 700; color: rgb(240,240,245);");
    auto *close = new QPushButton(QString(QChar(0xE8BB)));
    close->setObjectName("glyph");
    close->setToolTip("Hide the now playing view");
    close->setCursor(Qt::PointingHandCursor);
    connect(close, &QPushButton::clicked, this, [this] { if (onClose) onClose(); });
    auto *top = new QHBoxLayout;
    top->setContentsMargins(16, 12, 10, 6);
    top->addWidget(m_list, 1);
    top->addWidget(close);

    m_cover = new SquareCover;
    m_switch = new QPushButton;
    m_switch->setObjectName("npSwitch");
    m_switch->setCursor(Qt::PointingHandCursor);
    connect(m_switch, &QPushButton::clicked, this, [this] { if (onSwitchVideo) onSwitchVideo(); });

    m_title = new QLabel;
    m_title->setWordWrap(true);
    m_title->setCursor(Qt::PointingHandCursor);
    m_title->setToolTip("About this song");
    m_title->setStyleSheet("font-size: 16pt; font-weight: 800; color: rgb(240,240,245);");
    m_title->installEventFilter(this);
    m_artist = new QLabel;
    m_artist->setStyleSheet("color: rgb(170,168,182); font-weight: 600;");
    auto *about = new QPushButton(QString(QChar(0xE946)));   // an "i" in a circle
    about->setObjectName("glyph");
    about->setToolTip("About this song");
    about->setCursor(Qt::PointingHandCursor);
    connect(about, &QPushButton::clicked, this, [this] { if (onAbout) onAbout(); });
    auto *names = new QVBoxLayout;
    names->setSpacing(2);
    names->addWidget(m_title);
    names->addWidget(m_artist);
    auto *nameRow = new QHBoxLayout;
    nameRow->addLayout(names, 1);
    nameRow->addWidget(about, 0, Qt::AlignTop);

    // Lyrics preview: the line being sung and the next, on the cover's colour.
    m_lyricsCard = new ClickCard;
    m_lyricsCard->clicked = [this] { if (onOpenLyrics) onOpenLyrics(); };
    m_lyricsNow = new QLabel;
    m_lyricsNow->setWordWrap(true);
    m_lyricsNow->setStyleSheet("font-size: 14pt; font-weight: 800; color: white;");
    m_lyricsNext = new QLabel;
    m_lyricsNext->setWordWrap(true);
    m_lyricsNext->setStyleSheet("font-size: 14pt; font-weight: 800; color: rgba(255,255,255,0.45);");
    auto *lyricsBox = new QVBoxLayout(m_lyricsCard);
    lyricsBox->setContentsMargins(14, 12, 14, 14);
    lyricsBox->setSpacing(6);
    lyricsBox->addWidget(heading("Lyrics preview"));
    lyricsBox->addWidget(m_lyricsNow);
    lyricsBox->addWidget(m_lyricsNext);

    m_artistCard = new QFrame;
    m_artistCard->setObjectName("npCard");
    m_artistPhoto = new ArtistPhoto;
    m_artistName = new QLabel;
    m_artistName->setStyleSheet("font-size: 11.5pt; font-weight: 700; color: rgb(240,240,245);");
    m_artistAudience = new QLabel;
    m_artistAudience->setStyleSheet("color: rgb(170,168,182);");
    m_artistAbout = new QLabel;
    m_artistAbout->setWordWrap(true);
    m_artistAbout->setStyleSheet("color: rgb(200,198,210);");
    m_artistSource = new QLabel;
    m_artistSource->setStyleSheet("color: rgb(120,118,132); font-size: 8.5pt;");
    auto *artistText = new QVBoxLayout;
    artistText->setContentsMargins(14, 10, 14, 14);
    artistText->setSpacing(4);
    artistText->addWidget(m_artistName);
    artistText->addWidget(m_artistAudience);
    artistText->addWidget(m_artistAbout);
    artistText->addWidget(m_artistSource);
    auto *artistBox = new QVBoxLayout(m_artistCard);
    artistBox->setContentsMargins(0, 0, 0, 0);
    artistBox->setSpacing(0);
    artistBox->addWidget(m_artistPhoto);
    artistBox->addLayout(artistText);

    m_creditsCard = new QFrame;
    m_creditsCard->setObjectName("npCard");
    auto *showAll = new QPushButton("Show all");
    showAll->setObjectName("link");
    showAll->setCursor(Qt::PointingHandCursor);
    m_showAll = showAll;
    // The card grows to every credit, as Spotify's does; About this song is
    // the (i) by the name.
    connect(showAll, &QPushButton::clicked, this, [this] {
        m_creditsOpen = !m_creditsOpen;
        showCredits();
    });
    auto *creditsTop = new QHBoxLayout;
    creditsTop->addWidget(heading("Credits"), 1);
    creditsTop->addWidget(showAll);
    m_creditRows = new QVBoxLayout;
    m_creditRows->setSpacing(10);
    auto *creditsBox = new QVBoxLayout(m_creditsCard);
    creditsBox->setContentsMargins(14, 12, 14, 14);
    creditsBox->setSpacing(10);
    creditsBox->addLayout(creditsTop);
    creditsBox->addLayout(m_creditRows);

    m_nextCard = new ClickCard;
    m_nextCard->setToolTip("Play it now");
    m_nextCard->clicked = [this] { if (onPlayNext) onPlayNext(); };
    m_nextCover = new QLabel;
    m_nextCover->setFixedSize(44, 44);
    m_nextTitle = new QLabel;
    m_nextTitle->setStyleSheet("font-weight: 600; color: rgb(240,240,245);");
    m_nextArtist = new QLabel;
    m_nextArtist->setStyleSheet("color: rgb(170,168,182);");
    auto *nextNames = new QVBoxLayout;
    nextNames->setSpacing(2);
    nextNames->addWidget(m_nextTitle);
    nextNames->addWidget(m_nextArtist);
    auto *nextRow = new QHBoxLayout;
    nextRow->setSpacing(10);
    nextRow->addWidget(m_nextCover);
    nextRow->addLayout(nextNames, 1);
    auto *nextBox = new QVBoxLayout(m_nextCard);
    nextBox->setContentsMargins(14, 12, 14, 12);
    nextBox->setSpacing(10);
    nextBox->addWidget(heading("Next in queue"));
    nextBox->addLayout(nextRow);

    auto *content = new QWidget;
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(16, 4, 16, 16);
    column->setSpacing(12);
    column->addWidget(m_cover);
    column->addWidget(m_switch, 0, Qt::AlignLeft);
    column->addLayout(nameRow);
    column->addWidget(m_lyricsCard);
    column->addWidget(m_artistCard);
    column->addWidget(m_creditsCard);
    column->addWidget(m_nextCard);
    column->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setObjectName("npScroll");
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    scroll->setStyleSheet("QScrollArea#npScroll, QScrollArea#npScroll > QWidget > QWidget { background: transparent; }");

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(0);
    box->addLayout(top);
    box->addWidget(scroll, 1);

    setVideoState(false, false, false);
    setLyrics(Lyrics(), false);
    setArtist(nullptr);
    setCredits({}, false);
    setNext(QString(), QString(), QPixmap());
}

QWidget *NowPlayingPanel::scrollArea() const
{
    return findChild<QScrollArea *>("npScroll");
}

bool NowPlayingPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_title && event->type() == QEvent::MouseButtonRelease && onAbout)
        onAbout();
    return QWidget::eventFilter(watched, event);
}

void NowPlayingPanel::setList(const QString &name)
{
    m_list->setText(name);
}

void NowPlayingPanel::setSong(const QString &title, const QString &artist)
{
    m_title->setText(title);
    m_artist->setText(artist);
    m_artist->setVisible(!artist.isEmpty());
}

void NowPlayingPanel::setCover(const QPixmap &cover)
{
    m_cover->picture = cover;
    m_cover->update();
    const QColor tint = softTint(cover);
    m_lyricsCard->setStyleSheet(QStringLiteral("QFrame#npCard { background: %1; }").arg(tint.name()));
}

void NowPlayingPanel::setVideoState(bool available, bool video, bool busy)
{
    m_switch->setVisible(available);
    m_switch->setEnabled(!busy);
    // A screen for the video, a note for the song.
    m_switch->setText(busy ? QStringLiteral("Loading the video...")
                      : video ? QStringLiteral("Switch to audio") : QStringLiteral("Switch to video"));
    // The glyph as an icon: inside the text it sat lower than the words and
    // pushed them off the middle. A note for the song, a screen for the video.
    const qreal dpr = devicePixelRatioF();
    QPixmap glyph(QSize(16, 16) * dpr);
    glyph.setDevicePixelRatio(dpr);
    glyph.fill(Qt::transparent);
    if (!busy) {
        QPainter p(&glyph);
        p.setRenderHint(QPainter::TextAntialiasing);
        QFont icons(QStringLiteral("Segoe Fluent Icons"));
        icons.setPixelSize(14);
        p.setFont(icons);
        p.setPen(QColor(240, 240, 245));
        p.drawText(QRectF(0, 0, 16, 16), Qt::AlignCenter, QString(QChar(video ? 0xEC4F : 0xE714)));
    }
    m_switch->setIcon(QIcon(glyph));
    m_switch->setIconSize(QSize(busy ? 0 : 16, 16));
}

void NowPlayingPanel::setLyrics(const Lyrics &lyrics, bool searching)
{
    m_lyrics = lyrics;
    m_shownLine = -2;
    m_lyricsCard->setVisible(lyrics.found && (!lyrics.lines.isEmpty() || !lyrics.plain.trimmed().isEmpty()));
    Q_UNUSED(searching);
    showLyricLines();
}

void NowPlayingPanel::setPosition(double seconds)
{
    m_position = seconds;
    if (!m_lyrics.lines.isEmpty())
        showLyricLines();
}

void NowPlayingPanel::showLyricLines()
{
    if (!m_lyricsCard->isVisible())
        return;
    if (m_lyrics.lines.isEmpty()) {
        // Plain words only: the song's first lines.
        if (m_shownLine == -1)
            return;
        m_shownLine = -1;
        QStringList lines;
        for (const QString &l : m_lyrics.plain.split('\n'))
            if (!l.trimmed().isEmpty() && lines.size() < 3)
                lines << l.trimmed();
        m_lyricsNow->setText(lines.mid(0, 1).join('\n'));
        m_lyricsNext->setText(lines.mid(1).join('\n'));
        return;
    }
    // The line being sung (the first, before the singing starts), and the
    // next two that have words.
    int now = 0;
    for (int i = 0; i < m_lyrics.lines.size(); ++i)
        if (m_lyrics.lines[i].at <= m_position + 0.2)
            now = i;
    if (now == m_shownLine)
        return;
    m_shownLine = now;
    auto words = [this](int from, int count) {
        QStringList out;
        for (int i = from; i < m_lyrics.lines.size() && out.size() < count; ++i)
            if (!m_lyrics.lines[i].text.trimmed().isEmpty())
                out << m_lyrics.lines[i].text.trimmed();
        return out;
    };
    const QStringList current = words(now, 1);
    m_lyricsNow->setText(current.value(0, QString(QChar(0x266A))));
    int after = now + 1;
    while (after < m_lyrics.lines.size() && m_lyrics.lines[after].text.trimmed().isEmpty())
        ++after;
    m_lyricsNext->setText(words(after, 2).join('\n'));
}

void NowPlayingPanel::setArtist(const ArtistInfo *artist)
{
    m_artistCard->setVisible(artist && artist->found);
    if (!artist || !artist->found)
        return;
    m_artistPhoto->picture = artist->photo;
    m_artistPhoto->updateGeometry();
    m_artistPhoto->update();
    m_artistName->setText(artist->name);
    m_artistAudience->setText(artist->audience);
    m_artistAudience->setVisible(!artist->audience.isEmpty());
    // The start of the biography; the rest is on YouTube Music.
    QString about = artist->about;
    if (about.size() > 260) {
        about = about.left(260);
        about = about.left(about.lastIndexOf(' ')) + QStringLiteral("...");
    }
    m_artistAbout->setText(about);
    m_artistAbout->setVisible(!about.isEmpty());
    m_artistSource->setText(QStringLiteral("Source: ") + artist->source);
    m_artistSource->setVisible(!about.isEmpty() && !artist->source.isEmpty());
}

void NowPlayingPanel::setCredits(const QList<QPair<QString, QString>> &credits, bool asked)
{
    Q_UNUSED(asked);
    if (credits != m_credits)
        m_creditsOpen = false;   // another song: closed again
    m_credits = credits;
    showCredits();
}

void NowPlayingPanel::showCredits()
{
    const auto &credits = m_credits;
    while (QLayoutItem *item = m_creditRows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_showAll->setVisible(credits.size() > 3);
    m_showAll->setText(m_creditsOpen ? QStringLiteral("Show less") : QStringLiteral("Show all"));
    for (int i = 0; i < credits.size() && (m_creditsOpen || i < 3); ++i) {
        auto *row = new QWidget;
        auto *name = new QLabel(credits[i].second);
        name->setWordWrap(true);
        name->setStyleSheet("font-weight: 600; color: rgb(240,240,245);");
        auto *role = new QLabel(credits[i].first);
        role->setStyleSheet("color: rgb(170,168,182);");
        auto *box = new QVBoxLayout(row);
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(1);
        box->addWidget(name);
        box->addWidget(role);
        m_creditRows->addWidget(row);
    }
    m_creditsCard->setVisible(!credits.isEmpty());
}

void NowPlayingPanel::setNext(const QString &title, const QString &artist, const QPixmap &cover)
{
    m_nextCard->setVisible(!title.isEmpty());
    m_nextTitle->setText(title);
    m_nextArtist->setText(artist);
    m_nextArtist->setVisible(!artist.isEmpty());
    QPixmap square(88, 88);
    square.fill(Qt::transparent);
    QPainter p(&square);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(0, 0, 88, 88), 8, 8);
    p.setClipPath(shape);
    if (cover.isNull()) {
        p.fillRect(QRectF(0, 0, 88, 88), QColor(48, 46, 56));
    } else {
        const int side = qMin(cover.width(), cover.height());
        p.drawPixmap(QRectF(0, 0, 88, 88), cover,
                     QRectF((cover.width() - side) / 2.0, (cover.height() - side) / 2.0, side, side));
    }
    p.end();
    square.setDevicePixelRatio(2);
    m_nextCover->setPixmap(square);
}
