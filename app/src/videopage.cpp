#include "videopage.h"

#include "theme.h"
#include "tiles.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int kSideWidth = 400;          // the list on the right
constexpr int kTargetRole = Qt::UserRole + 40;
constexpr int kNowRole = Qt::UserRole + 41;

// The black area mpv draws into. A native window of its own, so mpv can be
// given its handle; painted black for the moment before mpv's first frame.
class Surface : public QWidget {
public:
    Surface()
    {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_DontCreateNativeAncestors);
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, Qt::black);
        setPalette(p);
        winId();   // the window exists from now on
    }
};

// A video in the list on the right: its thumbnail, title, and length, as in
// YouTube's "up next".
class VideoRow : public QStyledItemDelegate {
public:
    VideoRow(const VideoPage *page, QObject *parent) : QStyledItemDelegate(parent), m_page(page) {}
    std::function<QPixmap(const QString &)> cover;
    std::function<QString(const QString &)> length;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return {kSideWidth, int(84 * theme::zoom())};
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(2, 3, -2, -3);
        const bool now = index.data(kNowRole).toBool();
        const bool hover = option.state & QStyle::State_MouseOver;
        if (now || hover) {
            QPainterPath bg;
            bg.addRoundedRect(row, 8, 8);
            p->fillPath(bg, now ? theme::cardSelected() : theme::cardHover());
        }
        const QString target = index.data(kTargetRole).toString();
        const QRectF thumb(row.left() + 6, row.top() + 5, (row.height() - 10) * 16 / 9, row.height() - 10);
        tiles::paintCover(*p, thumb, cover ? cover(target) : QPixmap(), index.data(Qt::DisplayRole).toString(), 6);
        const QString len = length ? length(target) : QString();
        if (!len.isEmpty()) {
            QFont f = option.font;
            f.setPointSizeF(f.pointSizeF() * 0.85);
            f.setWeight(QFont::DemiBold);
            const QFontMetricsF fm(f);
            const QRectF badge(thumb.right() - fm.horizontalAdvance(len) - 12, thumb.bottom() - fm.height() - 6,
                               fm.horizontalAdvance(len) + 8, fm.height() + 2);
            QPainterPath b;
            b.addRoundedRect(badge, 4, 4);
            p->fillPath(b, QColor(0, 0, 0, 190));
            p->setFont(f);
            p->setPen(Qt::white);
            p->drawText(badge, Qt::AlignCenter, len);
        }
        const QRectF text(thumb.right() + 10, row.top() + 6, row.right() - thumb.right() - 16, row.height() - 12);
        QFont title = option.font;
        title.setWeight(QFont::DemiBold);
        p->setFont(title);
        p->setPen(now ? theme::accent() : theme::text());
        p->drawText(text, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, index.data(Qt::DisplayRole).toString());
        p->restore();
    }

private:
    const VideoPage *m_page;
};

QPushButton *action(const QString &glyph, const QString &label)
{
    auto *b = new QPushButton(glyph + QStringLiteral("  ") + label);
    b->setObjectName("videoAction");
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

} // namespace

VideoPage::VideoPage(QWidget *parent) : QWidget(parent)
{
    m_surface = new Surface;

    // While mpv plays in its own window, this stands where the picture was.
    m_away = new QWidget;
    m_away->setObjectName("videoAway");
    m_away->setAttribute(Qt::WA_StyledBackground);
    auto *awayText = new QLabel("Playing in mpv's own window");
    awayText->setObjectName("sectionTitle");
    awayText->setAlignment(Qt::AlignCenter);
    auto *back = new QPushButton("Bring it back here");
    back->setObjectName("primary");
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QPushButton::clicked, this, [this] { if (onBringBack) onBringBack(); });
    auto *awayBox = new QVBoxLayout(m_away);
    awayBox->addStretch();
    awayBox->addWidget(awayText);
    awayBox->addSpacing(12);
    awayBox->addWidget(back, 0, Qt::AlignHCenter);
    awayBox->addStretch();

    m_player = new QStackedWidget;
    m_player->addWidget(m_surface);
    m_player->addWidget(m_away);

    m_title = new QLabel;
    m_title->setObjectName("videoTitle");
    m_title->setWordWrap(true);
    m_channel = new QLabel;
    m_channel->setObjectName("statLabel");
    auto *popOut = action(QString(QChar(0xE8A7)), "Open in mpv's window");
    auto *full = action(QString(QChar(0xE740)), "Full screen");
    m_youtube = action(QString(QChar(0xE774)), "Watch on YouTube");
    connect(popOut, &QPushButton::clicked, this, [this] { if (onPopOut) onPopOut(); });
    connect(full, &QPushButton::clicked, this, [this] { if (onFullScreen) onFullScreen(); });
    connect(m_youtube, &QPushButton::clicked, this, [this] {
        if (!m_link.isEmpty())
            QDesktopServices::openUrl(QUrl(m_link));
    });
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(m_channel, 1);
    buttons->addWidget(popOut);
    buttons->addWidget(full);
    buttons->addWidget(m_youtube);
    m_info = new QWidget;
    auto *info = new QVBoxLayout(m_info);
    info->setContentsMargins(4, 12, 4, 0);
    info->setSpacing(6);
    info->addWidget(m_title);
    info->addLayout(buttons);

    auto *left = new QVBoxLayout;
    left->setSpacing(0);
    left->addWidget(m_player);
    left->addWidget(m_info);
    left->addStretch();

    m_listName = new QLabel;
    m_listName->setObjectName("sectionTitle");
    m_list = new QListWidget;
    m_list->setObjectName("songs");
    m_list->setMouseTracking(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    auto *rows = new VideoRow(this, m_list);
    rows->cover = [this](const QString &t) { return coverFor ? coverFor(t) : QPixmap(); };
    rows->length = [this](const QString &t) { return lengthFor ? lengthFor(t) : QString(); };
    m_list->setItemDelegate(rows);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (onPick)
            onPick(item->data(kTargetRole).toString());
    });
    m_side = new QWidget;
    m_side->setFixedWidth(kSideWidth);
    auto *side = new QVBoxLayout(m_side);
    side->setContentsMargins(0, 0, 0, 0);
    side->setSpacing(8);
    side->addWidget(m_listName);
    side->addWidget(m_list, 1);

    auto *box = new QHBoxLayout(this);
    box->setContentsMargins(24, 20, 20, 12);
    box->setSpacing(20);
    box->addLayout(left, 1);
    box->addWidget(m_side);
}

void VideoPage::setList(const QString &name, const QVector<Entry> &entries)
{
    m_listName->setText(name);
    m_list->clear();
    for (const Entry &e : entries) {
        auto *item = new QListWidgetItem(e.title);
        item->setData(kTargetRole, e.target);
        m_list->addItem(item);
    }
}

void VideoPage::setPlaying(const QString &target, const QString &title, const QString &channel, const QString &link)
{
    m_title->setText(title);
    m_channel->setText(channel);
    m_link = link;
    m_youtube->setVisible(!link.isEmpty());
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        const bool now = item->data(kTargetRole).toString() == target;
        item->setData(kNowRole, now);
        if (now)
            m_list->scrollToItem(item, QAbstractItemView::EnsureVisible);
    }
}

void VideoPage::setPoppedOut(bool out)
{
    m_player->setCurrentWidget(out ? m_away : m_surface);
}

void VideoPage::setFullScreen(bool full)
{
    m_full = full;
    m_info->setVisible(!full);
    m_side->setVisible(!full);
    layout()->setContentsMargins(full ? QMargins() : QMargins(24, 20, 20, 12));
    fitPlayer();
}

void VideoPage::refreshCovers() const
{
    m_list->viewport()->update();
}

void VideoPage::resizeEvent(QResizeEvent *)
{
    fitPlayer();
}

void VideoPage::fitPlayer()
{
    // 16:9 across the width it has, unless that would push the title off
    // the page; full screen takes all of it.
    if (m_full) {
        m_player->setFixedSize(size());
        return;
    }
    const int width = this->width() - kSideWidth - 64;
    const int height = qMin(width * 9 / 16, this->height() - 150);
    m_player->setFixedSize(qMax(320, height * 16 / 9), qMax(180, height));
}
