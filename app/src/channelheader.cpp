#include "channelheader.h"

#include "profile.h"
#include "theme.h"

#include <QDesktopServices>
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
    auto *column = new QVBoxLayout(this);
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

    theme::whenChanged(this, [this] {
        theme::style(m_name, QStringLiteral("color: @textBright; font-size: %1pt; font-weight: 800;").arg(m_folded ? 14 : 24));
        theme::style(m_follow, QStringLiteral(
            "QPushButton#followBig { background: @textBright; color: @window; border: none; border-radius: 17px;"
            " padding: 0 18px; min-height: 34px; max-height: 34px; font-weight: 600; }"
            "QPushButton#followBig:hover { background: @text; }"
            "QPushButton#followBig:checked { background: @raised; color: @textBright; }"
            "QPushButton#followBig:checked:hover { background: @pressed; }"));
        m_links->setText(m_links->text());
        m_banner->update();
        m_avatar->update();
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
    m_meta->clear();
    m_description->clear();
    m_links->clear();
    m_facts->clear();
    setLive(false);
    m_more = false;
    refreshPictures();
    setFolded(false);
}

void ChannelHeader::setPage(const ytmusic::ChannelPage &page)
{
    if (!page.name.isEmpty())
        m_name->setText(page.name);
    QStringList meta;
    if (!page.handle.isEmpty())
        meta << page.handle;
    if (!page.about.isEmpty())
        meta << page.about;
    m_meta->setText(meta.join(QStringLiteral("  •  ")));
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
    m_follow->setChecked(followed);
    m_follow->setText(followed ? QStringLiteral("Following") : QStringLiteral("Follow"));
}

void ChannelHeader::setLive(bool live)
{
    m_liveTag->setVisible(live);
    m_avatar->live = live;
    m_avatar->update();
}

void ChannelHeader::setFolded(bool folded, bool animate)
{
    const bool changes = folded != m_folded;
    const int before = height();
    m_folded = folded;
    m_banner->setVisible(!folded && m_hasBanner);
    const int side = qRound((folded ? 48 : 120) * theme::zoom());
    m_avatar->setFixedSize(side, side);
    m_descriptionBox->setVisible(!folded && !m_description->text().isEmpty());
    // The first lines as they are; all of it in at most ~8 lines that scroll.
    const int lines = QFontMetrics(m_description->font()).lineSpacing();
    m_descriptionBox->setFixedHeight(m_more ? qMin(m_description->heightForWidth(qMax(100, m_descriptionBox->width())) + 4, lines * 8)
                                            : qMin(m_description->heightForWidth(qMax(100, m_descriptionBox->width())) + 4, lines * 2 + 4));
    m_moreButton->setVisible(!folded && m_canMore);
    m_links->setVisible(!folded && m_more && !m_links->text().isEmpty());
    m_facts->setVisible(!folded && m_more && !m_facts->text().isEmpty());
    theme::style(m_name, QStringLiteral("color: @textBright; font-size: %1pt; font-weight: 800;").arg(folded ? 14 : 24));
    placeBanner();
    if (!animate || !changes || !isVisible()) {
        if (m_heightGlide)
            m_heightGlide->stop();
        setMaximumHeight(QWIDGETSIZE_MAX);
        return;
    }
    // From the height it had to the one it now wants, eased.
    const int after = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width()) : sizeHint().height();
    if (!m_heightGlide) {
        m_heightGlide = new QVariantAnimation(this);
        m_heightGlide->setDuration(220);
        m_heightGlide->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_heightGlide, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            setMaximumHeight(v.toInt());
            setMinimumHeight(v.toInt());
        });
        connect(m_heightGlide, &QVariantAnimation::finished, this, [this] {
            setMinimumHeight(0);
            setMaximumHeight(QWIDGETSIZE_MAX);
        });
    }
    m_heightGlide->stop();
    m_heightGlide->setStartValue(before);
    m_heightGlide->setEndValue(after);
    m_heightGlide->start();
}

void ChannelHeader::showMore(bool more)
{
    m_more = more;
    const QString text = more && m_aboutKnown && !m_about.description.isEmpty() ? m_about.description : m_preview;
    m_description->setText(text);
    m_moreButton->setText(more ? QStringLiteral("less") : QStringLiteral("more"));
    setFolded(m_folded);
}

void ChannelHeader::refreshPictures()
{
    m_banner->picture = m_pictures->get(m_bannerUrl);
    m_avatar->picture = m_pictures->get(m_avatarUrl);
    m_banner->update();
    m_avatar->update();
}

void ChannelHeader::placeBanner()
{
    // As wide as the page, about a sixth as tall, as YouTube draws it.
    m_banner->setFixedHeight(qBound(80, width() / 6, 210));
}

void ChannelHeader::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    placeBanner();
}

void ChannelHeader::wheelEvent(QWheelEvent *event)
{
    // Over the header the wheel scrolls the videos (the description box takes
    // its own while it can).
    if (onWheel)
        onWheel(event);
    event->accept();
}
