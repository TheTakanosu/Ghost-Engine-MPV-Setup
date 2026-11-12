#include "channelheader.h"

#include "profile.h"
#include "theme.h"
#include "tiles.h"

#include <QDesktopServices>
#include <QGraphicsOpacityEffect>
#include <QResizeEvent>
#include <QSpacerItem>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QWheelEvent>
#include <QUrl>
#include <QVariantAnimation>
#include <QVBoxLayout>

// The banner: the picture filling a wide rounded strip, cropped, not stretched.
class Banner : public QWidget {
public:
    using QWidget::QWidget;
    QPixmap picture;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath round;
        round.addRoundedRect(QRectF(rect()), 12, 12);
        if (picture.isNull()) {
            p.fillPath(round, theme::c().card);
            return;
        }
        p.setClipPath(round);
        const QSizeF fill = QSizeF(picture.size()).scaled(QSizeF(size()), Qt::KeepAspectRatioByExpanding);
        const QPointF at((width() - fill.width()) / 2, (height() - fill.height()) / 2);
        p.drawPixmap(QRectF(at, fill), picture, QRectF(picture.rect()));
    }
};

// The channel's picture in a circle, the card colour until it arrives.
class RoundPicture : public QWidget {
public:
    using QWidget::QWidget;
    QPixmap picture;
    bool live = false;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const qreal ring = live ? 3 : 0;
        const QRectF circle = QRectF(rect()).adjusted(ring + 1, ring + 1, -ring - 1, -ring - 1);
        QPainterPath round;
        round.addEllipse(circle);
        if (picture.isNull()) {
            p.fillPath(round, theme::c().raised);
        } else {
            p.save();
            p.setClipPath(round);
            p.drawPixmap(circle, picture, QRectF(picture.rect()));
            p.restore();
        }
        if (live) {
            p.setPen(QPen(QColor(0xE5, 0x2B, 0x2B), 2.5));
            p.drawEllipse(QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5));
        }
    }
};

ChannelHeader::ChannelHeader(RemotePictures *pictures, QWidget *parent) : QWidget(parent), m_pictures(pictures)
{
    // Two faces (Batin, 2026-11-12: squeezed in one layout as it folded,
    // the name, the description and the Joined line ran into each other).
    // The whole header is one block that slides up under the page's top as
    // the videos scroll, never squeezed; once it is nearly gone it fades out
    // and the one row fades in, the two never seen at once.
    m_body = new QWidget(this);
    auto *column = new QVBoxLayout(m_body);
    column->setContentsMargins(8, 0, 8, 4);
    column->setSpacing(14);

    m_banner = new Banner;
    column->addWidget(m_banner);

    auto *row = new QHBoxLayout;
    row->setSpacing(18);
    m_avatar = new RoundPicture;
    row->addWidget(m_avatar, 0, Qt::AlignTop);

    auto *text = new QVBoxLayout;
    text->setSpacing(4);
    auto *nameRow = new QHBoxLayout;
    nameRow->setSpacing(10);
    m_name = new QLabel;
    m_name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    nameRow->addWidget(m_name);
    m_liveTag = new QLabel(QStringLiteral("LIVE"));
    theme::style(m_liveTag, QStringLiteral("background: #D42020; color: white; font-weight: 700; font-size: 8pt;"
                                            "border-radius: 4px; padding: 1px 6px;"));
    m_liveTag->hide();
    nameRow->addWidget(m_liveTag, 0, Qt::AlignVCenter);
    nameRow->addStretch();
    text->addLayout(nameRow);
    m_meta = new QLabel;
    m_meta->setObjectName("heroInfo");
    text->addWidget(m_meta);

    m_description = new QLabel;
    m_description->setWordWrap(true);
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    theme::style(m_description, QStringLiteral("color: @textSoft; font-size: 9.5pt;"));
    m_moreButton = new QPushButton(QStringLiteral("more"));
    m_moreButton->setObjectName("link");
    m_moreButton->setCursor(Qt::PointingHandCursor);
    connect(m_moreButton, &QPushButton::clicked, this, [this] { showMore(!m_more); });
    // All of a long description scrolls in its own box, so the videos keep
    // the page; "more" sits right after it, as on YouTube.
    m_descriptionBox = new QScrollArea;
    m_descriptionBox->setWidget(m_description);
    m_descriptionBox->setWidgetResizable(true);
    m_descriptionBox->setFrameShape(QFrame::NoFrame);
    m_descriptionBox->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_descriptionBox->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));
    text->addWidget(m_descriptionBox);
    auto *moreRow = new QHBoxLayout;
    moreRow->setContentsMargins(0, 0, 0, 0);
    moreRow->addWidget(m_moreButton);
    moreRow->addStretch();
    text->addLayout(moreRow);

    // Links open in the browser; YouTube's own address for them is skipped,
    // the link itself is what is shown and opened.
    m_links = new QLabel;
    m_links->setWordWrap(true);
    m_links->setTextFormat(Qt::RichText);
    m_links->setOpenExternalLinks(false);
    connect(m_links, &QLabel::linkActivated, this, [](const QString &url) { QDesktopServices::openUrl(QUrl(url)); });
    m_links->hide();
    text->addWidget(m_links);
    m_facts = new QLabel;
    m_facts->setObjectName("heroInfo");
    m_facts->hide();
    text->addWidget(m_facts);

    m_follow = new QPushButton(QStringLiteral("Follow"));
    m_follow->setObjectName("followBig");
    m_follow->setCheckable(true);
    m_follow->setCursor(Qt::PointingHandCursor);
    m_follow->setToolTip("Its new videos on your Following page, its name on the left");
    connect(m_follow, &QPushButton::clicked, this, [this] {
        if (onFollow)
            onFollow(m_follow->isChecked());
    });
    auto *followRow = new QHBoxLayout;
    followRow->setContentsMargins(0, 6, 0, 0);
    followRow->addWidget(m_follow);
    followRow->addStretch();
    text->addLayout(followRow);
    text->addStretch();
    row->addLayout(text, 1);
    column->addLayout(row);

    // The one row: picture, name and counts, Follow.
    m_row = new QWidget(this);
    auto *small = new QHBoxLayout(m_row);
    small->setContentsMargins(8, 2, 8, 10);
    small->setSpacing(14);
    m_smallAvatar = new RoundPicture;
    // Centred on the words as they are seen, not on their boxes: the name's
    // box has room over its capitals the counts' has not under their line,
    // and the picture stood a little high (Batin, 2026-11-12).
    auto *avatarColumn = new QVBoxLayout;
    avatarColumn->setSpacing(0);
    m_avatarNudge = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    avatarColumn->addItem(m_avatarNudge);
    avatarColumn->addWidget(m_smallAvatar);
    small->addLayout(avatarColumn);
    small->setAlignment(avatarColumn, Qt::AlignVCenter);
    auto *smallText = new QVBoxLayout;
    smallText->setSpacing(2);
    m_smallName = new QLabel;
    m_smallMeta = new QLabel;
    m_smallMeta->setObjectName("heroInfo");
    smallText->addStretch();
    smallText->addWidget(m_smallName);
    smallText->addWidget(m_smallMeta);
    smallText->addStretch();
    small->addLayout(smallText, 1);
    m_smallFollow = new QPushButton(QStringLiteral("Follow"));
    m_smallFollow->setObjectName("followBig");
    m_smallFollow->setCheckable(true);
    m_smallFollow->setCursor(Qt::PointingHandCursor);
    m_smallFollow->setToolTip(m_follow->toolTip());
    connect(m_smallFollow, &QPushButton::clicked, this, [this] {
        if (onFollow)
            onFollow(m_smallFollow->isChecked());
    });
    small->addWidget(m_smallFollow, 0, Qt::AlignVCenter);
    m_bodyFade = new QGraphicsOpacityEffect(m_body);
    m_bodyFade->setEnabled(false);
    m_body->setGraphicsEffect(m_bodyFade);
    m_rowFade = new QGraphicsOpacityEffect(m_row);
    m_rowFade->setEnabled(false);
    m_row->setGraphicsEffect(m_rowFade);
    m_row->hide();

    theme::style(m_name, QStringLiteral("color: @textBright; font-size: 24pt; font-weight: 800;"));
    theme::style(m_smallName, QStringLiteral("color: @textBright; font-size: 14pt; font-weight: 800;"));
    theme::whenChanged(this, [this] {
        for (QPushButton *follow : {m_follow, m_smallFollow})
        theme::style(follow, QStringLiteral(
            "QPushButton#followBig { background: @textBright; color: @window; border: none; border-radius: 17px;"
            " padding: 0 18px; min-height: 34px; max-height: 34px; font-weight: 600; }"
            "QPushButton#followBig:hover { background: @text; }"
            "QPushButton#followBig:checked { background: @raised; color: @textBright; }"
            "QPushButton#followBig:checked:hover { background: @pressed; }"));
        m_links->setText(m_links->text());
        m_banner->update();
        m_avatar->update();
        m_smallAvatar->update();
        if (m_gap)
            m_gap->update();   // its shade follows the theme
    });
    connect(m_pictures, &RemotePictures::ready, this, [this](const QString &url) {
        if (url == m_bannerUrl || url == m_avatarUrl)
            refreshPictures();
    });
    setFolded(false);
}

void ChannelHeader::reset(const QString &channelId, const QString &name)
{
    m_id = channelId;
    m_bannerUrl.clear();
    m_avatarUrl.clear();
    m_hasBanner = false;
    m_canMore = false;
    m_about = {};
    m_aboutKnown = false;
    m_preview.clear();
    m_name->setText(name.isEmpty() ? QStringLiteral("Loading the channel...") : name);
    m_smallName->setText(m_name->text());
    m_meta->clear();
    m_smallMeta->clear();
    m_description->clear();
    m_links->clear();
    m_facts->clear();
    setLive(false);
    m_more = false;
    m_scrolled = 0;
    refreshPictures();
    showMore(false);
}

void ChannelHeader::setPage(const ytmusic::ChannelPage &page)
{
    if (!page.name.isEmpty()) {
        m_name->setText(page.name);
        m_smallName->setText(page.name);
    }
    QStringList meta;
    if (!page.handle.isEmpty())
        meta << page.handle;
    if (!page.about.isEmpty())
        meta << page.about;
    m_meta->setText(meta.join(QStringLiteral("  •  ")));
    m_smallMeta->setText(m_meta->text());
    const auto full = [](const QString &url) {
        return url.startsWith(QLatin1String("//")) ? QStringLiteral("https:") + url : url;
    };
    m_bannerUrl = full(page.banner);
    m_avatarUrl = full(page.avatar);
    m_hasBanner = !m_bannerUrl.isEmpty();
    m_preview = page.description;
    m_canMore = !page.aboutToken.isEmpty();
    refreshPictures();
    showMore(m_more);
}

void ChannelHeader::setAbout(const ytmusic::ChannelAbout &about)
{
    m_about = about;
    m_aboutKnown = true;
    QStringList links;
    for (const auto &[title, address] : about.links) {
        const QString url = address.startsWith(QLatin1String("http")) ? address : QStringLiteral("https://") + address;
        links << QStringLiteral("<a href=\"%1\" style=\"color: %2; text-decoration: none;\">%3</a>")
                     .arg(url.toHtmlEscaped(), theme::accentText().name(), title.toHtmlEscaped());
    }
    m_links->setText(links.join(QStringLiteral("&nbsp;&nbsp;·&nbsp;&nbsp;")));
    QStringList facts;
    for (const QString &f : {about.joined, about.views, about.country})
        if (!f.isEmpty())
            facts << f;
    m_facts->setText(facts.join(QStringLiteral("  •  ")));
    showMore(m_more);
}

void ChannelHeader::setFollowed(bool followed)
{
    for (QPushButton *follow : {m_follow, m_smallFollow}) {
        follow->setChecked(followed);
        follow->setText(followed ? QStringLiteral("Following") : QStringLiteral("Follow"));
    }
}

void ChannelHeader::setLive(bool live)
{
    m_liveTag->setVisible(live);
    for (RoundPicture *picture : {m_avatar, m_smallAvatar}) {
        picture->live = live;
        picture->update();
    }
}

void ChannelHeader::setFolded(bool folded, bool)
{
    setScrolled(folded ? QWIDGETSIZE_MAX / 2 : 0);
}

void ChannelHeader::measure()
{
    // The block at its own size: the banner, the picture, the description in
    // its box (two lines, or up to eight that scroll), the links and facts
    // under "more".
    m_banner->setVisible(m_hasBanner);
    const int side = qRound(120 * theme::zoom());
    m_avatar->setFixedSize(side, side);
    const int smallSide = qRound(48 * theme::zoom());
    m_smallAvatar->setFixedSize(smallSide, smallSide);
    {
        // The words' ink runs from the name's capitals to the counts'
        // baseline; the picture, centred with a spacer over it, moves down by
        // half the spacer, which is the half-difference times two.
        const QFontMetricsF name(m_smallName->font()), meta(m_smallMeta->font());
        const qreal over = name.ascent() - name.capHeight();   // empty above the capitals
        const qreal under = meta.descent();                    // empty below the baseline
        const int nudge = qMax(0, qRound(over - under));
        m_avatarNudge->changeSize(0, nudge, QSizePolicy::Minimum, QSizePolicy::Fixed);
        m_row->layout()->invalidate();
    }
    m_descriptionBox->setVisible(!m_description->text().isEmpty());
    const int lines = QFontMetrics(m_description->font()).lineSpacing();
    const int boxWidth = m_descriptionBox->width() > 100 ? m_descriptionBox->width() : qMax(100, width() - side - 60);
    const int wanted = m_description->heightForWidth(boxWidth) + 4;
    m_descriptionBox->setFixedHeight(qMin(wanted, m_more ? lines * 8 : lines * 2 + 4));
    m_moreButton->setVisible(m_canMore);
    m_links->setVisible(m_more && !m_links->text().isEmpty());
    m_facts->setVisible(m_more && !m_facts->text().isEmpty());
    placeBanner();
    const int w = qMax(1, width());
    QLayout *full = m_body->layout();
    full->invalidate();
    m_openHeight = full->hasHeightForWidth() ? full->totalHeightForWidth(w) : full->totalSizeHint().height();
    QLayout *one = m_row->layout();
    m_foldedHeight = one->hasHeightForWidth() ? one->totalHeightForWidth(w) : one->totalSizeHint().height();
    m_foldedHeight = qMin(m_foldedHeight, m_openHeight);
}

void ChannelHeader::setScrolled(int scrolled)
{
    // As a list's header on the music side: it shrinks as far as the videos
    // are scrolled, one for one, down to the one row; back at the top it is
    // whole again. Only the block's place and two opacities change on the
    // way, nothing is laid out again, so the scrolling stays smooth.
    m_scrolled = qMax(0, scrolled);
    if (m_openHeight <= 0)
        measure();
    const int h = qBound(m_foldedHeight, m_openHeight - m_scrolled, m_openHeight);
    m_folded = h <= m_foldedHeight;
    m_body->setGeometry(0, h - m_openHeight, width(), m_openHeight);
    m_row->setGeometry(0, 0, width(), m_foldedHeight);
    // 0 folded, 1 whole. The block is gone before the row starts to show.
    const qreal p = m_openHeight > m_foldedHeight ? qreal(h - m_foldedHeight) / (m_openHeight - m_foldedHeight) : 1.0;
    const auto fade = [](QWidget *w, QGraphicsOpacityEffect *effect, qreal opacity) {
        opacity = qBound(0.0, opacity, 1.0);
        w->setVisible(opacity > 0.01);
        effect->setEnabled(opacity < 0.99);
        effect->setOpacity(opacity);
    };
    fade(m_body, m_bodyFade, (p - 0.22) / 0.33);
    fade(m_row, m_rowFade, 1.0 - p / 0.2);
    // The gap to the videos is the header's own (see placeGap).
    m_below = qRound(10 * theme::zoom());
    const int tall = h + m_below;
    if (height() != tall || minimumHeight() != tall || maximumHeight() != tall) {
        setFixedHeight(tall);
        // The videos under it move in the same frame, not one later.
        if (!m_inResize && parentWidget() && parentWidget()->layout())
            parentWidget()->layout()->activate();
    }
    placeGap();
}

void ChannelHeader::showMore(bool more)
{
    m_more = more;
    const QString text = more && m_aboutKnown && !m_about.description.isEmpty() ? m_about.description : m_preview;
    m_description->setText(text);
    m_moreButton->setText(more ? QStringLiteral("less") : QStringLiteral("more"));
    remeasure();
}

void ChannelHeader::remeasure()
{
    measure();
    setScrolled(m_scrolled);
}

void ChannelHeader::refreshPictures()
{
    m_banner->picture = m_pictures->get(m_bannerUrl);
    m_avatar->picture = m_pictures->get(m_avatarUrl);
    m_smallAvatar->picture = m_avatar->picture;
    m_banner->update();
    m_avatar->update();
    m_smallAvatar->update();
}

void ChannelHeader::placeBanner()
{
    // As wide as the page, about a sixth as tall, as YouTube draws it.
    m_banner->setFixedHeight(qBound(80, width() / 6, 210));
}

void ChannelHeader::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_inResize = true;
    if (event->size().width() != event->oldSize().width())
        remeasure();
    m_inResize = false;
    placeGap();
}

void ChannelHeader::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    placeGap();
}

void ChannelHeader::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    remeasure();
}

void ChannelHeader::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (m_gap)
        m_gap->hide();
}

void ChannelHeader::placeGap()
{
    // The header is the page's own card; once videos pass under it, a soft
    // shadow falls from its bottom edge over them, coming in over the first
    // 40 px of scrolling (Batin and Kaptan, 2026-11-12: the strip of ground
    // that parted it from the videos read as a black gap). Laid over the
    // videos across the page's width, the mouse going through; it pushes
    // nothing.
    QWidget *page = parentWidget();
    if (!page || !isVisible())
        return;
    if (!m_gap)
        m_gap = new tiles::Shade(page, true);
    auto *shade = static_cast<tiles::Shade *>(m_gap);
    // The videos are cut off at the header's bottom edge and nowhere lower
    // (Batin, 2026-11-12: a strip under the folded header showed the
    // captions of the row above): the gap between them is the header's own
    // bottom, painted and lying over the videos, and the shadow starts on
    // that edge.
    const int edge = geometry().bottom() + 1;
    shade->setGeometry(0, edge, page->width(), tiles::Shade::depth());
    shade->setStrength(m_scrolled / 40.0);
    raise();
    shade->raise();
}

void ChannelHeader::paintEvent(QPaintEvent *)
{
    // Opaque, the page card's colour, the strip under it too: nothing of the
    // videos shows through or above their edge.
    QPainter p(this);
    p.fillRect(rect(), theme::panel());
}

void ChannelHeader::wheelEvent(QWheelEvent *event)
{
    // Over the header the wheel scrolls the videos (the description box takes
    // its own while it can).
    if (onWheel)
        onWheel(event);
    event->accept();
}
