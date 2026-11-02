#include "songinfo.h"
#include "theme.h"

#include <QDate>
#include <QDesktopServices>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>

namespace songinfo {

QStringList ytDlpArguments(const QString &url)
{
    // The first line: id, album, dates and channel, tab-separated; the rest
    // is the description.
    const QString facts = QStringLiteral("%(id)s\t%(album)s\t%(release_date)s\t%(release_year)s\t%(upload_date)s\t%(channel_id)s");
    return {"--skip-download", "--no-warnings", "--encoding", "utf-8", "--playlist-items", "1",
            "--print", facts, "--print", "description", url};
}

static QString given(const QString &value)
{
    const QString v = value.trimmed();
    return v == QLatin1String("NA") || v == QLatin1String("None") ? QString() : v;
}

static QString dateText(const QString &yyyymmdd)
{
    const QDate date = QDate::fromString(yyyymmdd, QStringLiteral("yyyyMMdd"));
    return date.isValid() ? QLocale(QLocale::English).toString(date, QStringLiteral("MMM d, yyyy")) : QString();
}

// A line nobody reading about a song wants: links, calls to follow or buy,
// hashtags, copyright, decorations.
static bool isNoise(const QString &line)
{
    static const QRegularExpression noise(
        QStringLiteral("https?:|www\\.|\\.(com|net|org|io|ly|me|tv)\\b|@\\w|#\\w|subscribe|abone|follow|takip|"
                       "instagram|tiktok|twitter|facebook|spotify|apple music|itunes|deezer|soundcloud|"
                       "youtube music|stream|download|listen|dinle|indir|contact|iletişim|booking|business|"
                       "management|copyright|all rights|tüm hakları|℗|©|official (music |lyric )?video|"
                       "lyric video|klip|playlist|merch|ticket|bilet|patreon|discord|website|platform|yayında|out now|"
                       "available (on|now)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression letter(QStringLiteral("\\p{L}"), QRegularExpression::UseUnicodePropertiesOption);
    return noise.match(line).hasMatch() || !letter.match(line).hasMatch();
}

static const QRegularExpression &creditLine()
{
    // "Composer: Name", "Söz - Müzik: Name", "Prod. by: Name"
    static const QRegularExpression re(QStringLiteral("^([\\p{L}][\\p{L} .&/\\-]{1,28})\\s*[:：]\\s*(.{2,80})$"),
                                       QRegularExpression::UseUnicodePropertiesOption);
    return re;
}

Facts parse(const QString &printed)
{
    Facts facts;
    const qsizetype newline = printed.indexOf('\n');
    const QStringList head = printed.left(newline < 0 ? printed.size() : newline).split('\t');
    const QString description = newline < 0 ? QString() : printed.mid(newline + 1);
    facts.id = given(head.value(0));
    facts.album = given(head.value(1));
    facts.channelId = given(head.value(5));
    facts.released = dateText(given(head.value(2)));
    if (facts.released.isEmpty())
        facts.released = given(head.value(3));

    const QStringList lines = description.split('\n');
    if (description.startsWith(QLatin1String("Provided to YouTube by "))) {
        // YouTube Music's own page: everything is in known places.
        facts.label = lines.value(0).mid(23).trimmed();
        static const QRegularExpression released(QStringLiteral("^Released on:\\s*(\\d{4})-(\\d{2})-(\\d{2})"));
        for (const QString &raw : lines) {
            const QString line = raw.trimmed();
            const auto r = released.match(line);
            if (r.hasMatch()) {
                facts.released = dateText(r.captured(1) + r.captured(2) + r.captured(3));
                continue;
            }
            if (line.startsWith(QStringLiteral("℗")) && facts.label.isEmpty())
                facts.label = line.mid(1).trimmed();
            const auto c = creditLine().match(line);
            if (c.hasMatch() && !line.startsWith(QLatin1String("Released on")))
                facts.credits.append({c.captured(1).trimmed(), c.captured(2).trimmed()});
        }
        return facts;
    }

    // Anyone else's description: credits where they are written as such,
    // and the first plain paragraphs, stopping at lyrics (a run of short
    // lines) and at anything that is not about the song.
    QStringList about;
    QStringList paragraph;
    bool stopped = false;
    auto endParagraph = [&] {
        if (paragraph.isEmpty() || stopped)
            return;
        int shortLines = 0;
        for (const QString &l : std::as_const(paragraph))
            shortLines += l.size() < 45;
        if (paragraph.size() >= 4 && shortLines * 4 >= paragraph.size() * 3) {
            stopped = true;   // lyrics or a poem: the lyrics page has them
            return;
        }
        about << paragraph.join('\n');
        paragraph.clear();
    };
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            endParagraph();
            paragraph.clear();
            continue;
        }
        if (isNoise(line))
            continue;
        if (line.endsWith(':') || line.endsWith(';') || line.endsWith(QStringLiteral("👇")))
            continue;   // leads into something left out (the lyrics, links)
        const auto c = creditLine().match(line);
        if (c.hasMatch()) {
            if (facts.credits.size() < 8)
                facts.credits.append({c.captured(1).trimmed(), c.captured(2).trimmed()});
            continue;
        }
        if (!stopped)
            paragraph << line;
    }
    endParagraph();
    QString text;
    for (const QString &p : std::as_const(about)) {
        if (!text.isEmpty() && text.size() + p.size() > 500)
            break;
        text += (text.isEmpty() ? QString() : QStringLiteral("\n\n")) + p;
    }
    facts.about = text.left(700);
    return facts;
}

}

// -- the window -----------------------------------------------------------------

SongInfoDialog::SongInfoDialog(const QString &title, const QString &artist, QWidget *parent) : QDialog(parent)
{
    setWindowTitle("About this song");
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(520);

    m_cover = new QLabel;
    m_cover->setFixedSize(150, 150);
    auto *kicker = new QLabel("SONG");
    kicker->setObjectName("kicker");
    auto *name = new QLabel(title);
    name->setObjectName("videoTitle");
    name->setWordWrap(true);
    name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *by = new QLabel(artist);
    by->setObjectName("videoChannel");
    by->setVisible(!artist.isEmpty());
    m_status = new QLabel("Asking YouTube...");
    m_status->setObjectName("statLabel");
    m_rows = new QGridLayout;
    m_rows->setHorizontalSpacing(14);
    m_rows->setVerticalSpacing(4);
    auto *side = new QVBoxLayout;
    side->setSpacing(4);
    side->addWidget(kicker);
    side->addWidget(name);
    side->addWidget(by);
    side->addSpacing(8);
    side->addLayout(m_rows);
    side->addWidget(m_status);
    side->addStretch();
    auto *top = new QHBoxLayout;
    top->setSpacing(18);
    top->addWidget(m_cover, 0, Qt::AlignTop);
    top->addLayout(side, 1);

    m_creditsTitle = new QLabel("Credits");
    m_creditsTitle->setObjectName("panelTitle");
    m_credits = new QGridLayout;
    m_credits->setHorizontalSpacing(14);
    m_credits->setVerticalSpacing(4);
    m_aboutTitle = new QLabel("From the YouTube page");
    m_aboutTitle->setObjectName("panelTitle");
    m_about = new QLabel;
    m_about->setWordWrap(true);
    m_about->setObjectName("heroInfo");
    m_about->setTextInteractionFlags(Qt::TextSelectableByMouse);
    for (QWidget *w : {static_cast<QWidget *>(m_creditsTitle), static_cast<QWidget *>(m_aboutTitle), static_cast<QWidget *>(m_about)})
        w->hide();

    m_youtube = new QPushButton("Watch on YouTube");
    m_youtube->setObjectName("link");
    m_youtube->setCursor(Qt::PointingHandCursor);
    m_youtube->hide();
    connect(m_youtube, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://www.youtube.com/watch?v=") + m_id));
    });
    auto *close = new QPushButton("Close");
    close->setObjectName("primary");
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_youtube);
    buttons->addStretch();
    buttons->addWidget(close);

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(22, 22, 22, 16);
    box->setSpacing(10);
    box->addLayout(top);
    box->addWidget(m_creditsTitle);
    box->addLayout(m_credits);
    box->addWidget(m_aboutTitle);
    box->addWidget(m_about);
    box->addSpacing(4);
    box->addLayout(buttons);
    // As tall as what it says: pulled taller, it only opened a gap under the
    // cover.
    box->setSizeConstraint(QLayout::SetFixedSize);
    setCover(QPixmap());
}

void SongInfoDialog::setCover(const QPixmap &cover)
{
    // The thumbnail's middle square, round-cornered as every cover is.
    const qreal dpr = devicePixelRatioF();
    QPixmap square(QSize(150, 150) * dpr);
    square.setDevicePixelRatio(dpr);
    square.fill(Qt::transparent);
    QPainter p(&square);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(0, 0, 150, 150), 8, 8);
    p.setClipPath(shape);
    if (cover.isNull()) {
        p.fillRect(QRectF(0, 0, 150, 150), theme::c().raised);
    } else {
        const int side = qMin(cover.width(), cover.height());
        const QRect middle((cover.width() - side) / 2, (cover.height() - side) / 2, side, side);
        p.drawPixmap(QRectF(0, 0, 150, 150), cover, middle);
    }
    p.end();
    m_cover->setPixmap(square);
}

void SongInfoDialog::addRow(const QString &name, const QString &value)
{
    auto *key = new QLabel(name);
    key->setObjectName("statLabel");
    auto *text = new QLabel(value);
    text->setWordWrap(true);
    text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    const int row = m_rows->rowCount();
    m_rows->addWidget(key, row, 0, Qt::AlignTop);
    m_rows->addWidget(text, row, 1);
    m_rows->setColumnStretch(1, 1);
}

void SongInfoDialog::setFacts(const songinfo::Facts &facts)
{
    m_id = facts.id;
    m_youtube->setVisible(!m_id.isEmpty());
    if (!facts.album.isEmpty())
        addRow("Album", facts.album);
    if (!facts.released.isEmpty())
        addRow("Released", facts.released);
    if (!facts.label.isEmpty())
        addRow("Label", facts.label);
    m_status->setVisible(facts.isEmpty());
    m_status->setText("YouTube has nothing more to say about this song.");
    for (const auto &[role, who] : facts.credits) {
        auto *key = new QLabel(role);
        key->setObjectName("statLabel");
        auto *name = new QLabel(who);
        name->setWordWrap(true);
        name->setTextInteractionFlags(Qt::TextSelectableByMouse);
        const int row = m_credits->rowCount();
        m_credits->addWidget(key, row, 0, Qt::AlignTop);
        m_credits->addWidget(name, row, 1);
        m_credits->setColumnStretch(1, 1);
    }
    m_creditsTitle->setVisible(!facts.credits.isEmpty());
    m_about->setText(facts.about);
    m_about->setVisible(!facts.about.isEmpty());
    m_aboutTitle->setVisible(!facts.about.isEmpty());
    adjustSize();
}

void SongInfoDialog::setFailed()
{
    m_status->setText("YouTube didn't answer. Try again in a moment.");
}
