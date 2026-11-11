#include "listheader.h"

#include "icons.h"
#include "theme.h"
#include "tiles.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr int kHeaderTall = 264;    // scrolled to the top
constexpr int kHeaderShort = 92;    // ... and once the songs are scrolled
constexpr qreal kFoldDistance = 170; // the scrolling over which it folds
} // namespace

PlaylistHeader::PlaylistHeader(Source source, QWidget *parent) : QWidget(parent), m_source(std::move(source)) {}

QRect PlaylistHeader::coverRect() const
{
    const int margin = height() > 160 * theme::zoom() ? 28 : 14;
    const int side = height() - 2 * margin;
    return {margin + 4, margin, side, side};
}

void PlaylistHeader::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QVector<QPixmap> covers = m_source();
    QColor tint = theme::placeholderFor(m_name);
    if (m_tint.isValid()) {
        tint = m_tint;
    } else if (!covers.isEmpty() && !covers.first().isNull()) {
        // The first cover's average colour, kept dark enough for white
        // text on top.
        const QColor avg = covers.first().toImage()
            .scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixelColor(0, 0);
        tint = QColor::fromHsv(avg.hsvHue(), qMin(avg.hsvSaturation(), 150), qBound(70, avg.value(), 120));
    }
    // On a light theme the title is dark, so the wash is a pale tint of
    // the same colour instead.
    if (theme::c().light)
        tint = QColor::fromHsv(tint.hsvHue(), qMin(tint.hsvSaturation(), 70), 232);
    QColor clear = theme::window();
    clear.setAlpha(0);
    QLinearGradient wash(0, 0, 0, height());
    wash.setColorAt(0, tint);
    wash.setColorAt(1, clear);
    QPainterPath top;
    top.addRoundedRect(QRectF(rect()).adjusted(0, 0, 0, 20), 12, 12);
    p.fillPath(top, wash);
    // A soft shadow under the cover lifts it off the wash.
    const QRect c = coverRect();
    for (int i = 1; i <= 6; ++i) {
        QPainterPath shadow;
        const qreal round = tiles::playlistRadius(QRectF(c), 10) + i;
        shadow.addRoundedRect(QRectF(c).adjusted(-i, -i + 4, i, i + 4), round, round);
        p.fillPath(shadow, QColor(0, 0, 0, 14));
    }
    tiles::paintCollage(p, c, covers, m_name, 10);
}

namespace listheader {

int tall() { return qRound(kHeaderTall * theme::zoom()); }
int folded() { return qRound(kHeaderShort * theme::zoom()); }

int tallFor(int pageHeight)
{
    return qMin(tall(), qMax(folded() + 60, pageHeight / 2));
}

qreal foldFor(int scrolled)
{
    return qBound(0.0, scrolled / kFoldDistance, 1.0);
}

int heightFor(qreal fold, int tallNow)
{
    return int(tallNow - fold * (tallNow - folded()));
}

qreal foldOf(const PlaylistHeader *header, int tallNow)
{
    if (tallNow <= folded())
        return 1.0;
    return qBound(0.0, qreal(tallNow - header->height()) / (tallNow - folded()), 1.0);
}

void arrange(PlaylistHeader *header, QHBoxLayout *row, QLabel *kicker, QLabel *info)
{
    const QRect cover = header->coverRect();
    const qreal z = theme::zoom();
    const bool isTall = header->height() > 160 * z;
    row->setContentsMargins(cover.right() + (isTall ? 26 : 16), cover.top(), 28, cover.top());
    if (kicker)
        kicker->setVisible(isTall);
    if (info)
        info->setVisible(header->height() > 120 * z);
}

qreal titlePoints(const QFont &base, const QString &name, int available, qreal fold, QString *elided)
{
    qreal size = 40 - fold * 20;
    QFont font = base;
    font.setWeight(QFont::ExtraBold);
    for (; size > 18; size -= 1) {
        font.setPointSizeF(size);
        if (QFontMetrics(font).horizontalAdvance(name) <= available)
            break;
    }
    font.setPointSizeF(size);
    if (elided)
        *elided = QFontMetrics(font).elidedText(name, Qt::ElideRight, qMax(40, available));
    return size;
}

namespace {
// The clock over the lengths, as the library's list page draws its own.
class ClockLabel : public QWidget {
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        icons::paint(p, icons::Icon::Clock, QRectF(width() - 25, 1, 16, 16), theme::muted());
    }
};
} // namespace

QWidget *plainColumns()
{
    auto *columns = new QWidget;
    columns->setObjectName("columns");
    columns->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(columns);
    row->setContentsMargins(0, 6, 0, 8);
    row->setSpacing(0);
    auto label = [](const QString &text) {
        auto *l = new QLabel(text);
        l->setObjectName("columnLabel");
        return l;
    };
    auto *hash = label(QStringLiteral("#"));
    hash->setFixedWidth(44);
    hash->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    row->addWidget(hash);
    row->addSpacing(4);
    row->addWidget(label(QStringLiteral("Title")));
    row->addSpacing(6);
    row->addWidget(label(QStringLiteral("·")));
    row->addSpacing(6);
    row->addWidget(label(QStringLiteral("Artist")));
    row->addStretch(1);
    auto *clock = new ClockLabel;
    clock->setFixedSize(tiles::kDurationColumn, 18);
    row->addWidget(clock);
    row->addSpacing(8 + 12);   // the rows' right margin, and the scroll bar beside them
    return columns;
}

} // namespace listheader
