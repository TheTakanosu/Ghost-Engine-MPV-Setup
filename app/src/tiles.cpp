#include "tiles.h"

#include "icons.h"
#include "theme.h"

#include <QPainter>
#include <QPainterPath>

namespace tiles {

void paintCover(QPainter &painter, const QRectF &rect, const QPixmap &pixmap,
                const QString &name, qreal radius)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(rect, radius, radius);
    painter.setClipPath(clip);

    if (!pixmap.isNull()) {
        const QSizeF scaled = QSizeF(pixmap.size()).scaled(rect.size(), Qt::KeepAspectRatioByExpanding);
        const QRectF target(rect.center() - QPointF(scaled.width() / 2, scaled.height() / 2), scaled);
        painter.drawPixmap(target, pixmap, pixmap.rect());
    } else {
        painter.fillRect(rect, theme::placeholderFor(name));
        QFont font = painter.font();
        font.setPixelSize(qMax(10, int(rect.height() * 0.42)));
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(QColor(255, 255, 255, 150));
        painter.drawText(rect, Qt::AlignCenter, name.left(1).toUpper());
    }
    painter.restore();
}

void paintCollage(QPainter &painter, const QRectF &rect, const QVector<QPixmap> &covers,
                  const QString &name, qreal radius)
{
    QVector<QPixmap> ready;
    for (const QPixmap &p : covers)
        if (!p.isNull())
            ready << p;
    if (ready.size() < 4) {
        paintCover(painter, rect, ready.value(0), name, radius);
        return;
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(rect, radius, radius);
    painter.setClipPath(clip);
    const qreal w = rect.width() / 2, h = rect.height() / 2;
    for (int i = 0; i < 4; ++i) {
        const QRectF cell(rect.left() + (i % 2) * w, rect.top() + (i / 2) * h, w, h);
        paintCover(painter, cell, ready[i], name, 0);
    }
    painter.restore();
}

TileDelegate::TileDelegate(int size, CoversFor coversFor, QObject *parent)
    : QStyledItemDelegate(parent), m_size(size), m_coversFor(std::move(coversFor)) {}

QSize TileDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return {m_size + 24, m_size + 70};
}

void TileDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const
{
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    const QRectF cell = QRectF(option.rect).adjusted(4, 4, -4, -4);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (selected || hover) {
        QPainterPath bg;
        bg.addRoundedRect(cell, 12, 12);
        painter->fillPath(bg, selected ? theme::cardSelected() : theme::cardHover());
    }

    const QRectF cover(cell.left() + 8, cell.top() + 8, cell.width() - 16, cell.width() - 16);
    const QString name = index.data(Qt::DisplayRole).toString();
    paintCollage(*painter, cover, m_coversFor(index), name, 8);

    QFont titleFont = option.font;
    titleFont.setWeight(QFont::DemiBold);
    painter->setFont(titleFont);
    painter->setPen(theme::text());
    const QRectF titleRect(cover.left(), cover.bottom() + 8, cover.width(), 20);
    painter->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(titleFont).elidedText(name, Qt::ElideRight, int(titleRect.width())));

    painter->setFont(option.font);
    painter->setPen(theme::muted());
    painter->drawText(QRectF(cover.left(), titleRect.bottom() + 2, cover.width(), 18),
                      Qt::AlignLeft | Qt::AlignVCenter, index.data(kSubtitleRole).toString());
    painter->restore();
}

RowDelegate::RowDelegate(CoversFor coversFor, QObject *parent)
    : QStyledItemDelegate(parent), m_coversFor(std::move(coversFor)) {}

QSize RowDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return {m_compact ? 64 : 240, 64};
}

void RowDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                        const QModelIndex &index) const
{
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    const QRectF row = QRectF(option.rect).adjusted(2, 2, -2, -2);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (selected || hover) {
        QPainterPath bg;
        bg.addRoundedRect(row, 8, 8);
        painter->fillPath(bg, selected ? theme::cardSelected() : theme::cardHover());
    }
    const qreal side = row.height() - 12;
    const QRectF cover(m_compact ? row.center().x() - side / 2 : row.left() + 6, row.top() + 6, side, side);
    const QString name = index.data(Qt::DisplayRole).toString();
    paintCollage(*painter, cover, m_coversFor(index), name, 6);

    if (!m_compact) {
        const QRectF text(cover.right() + 12, row.top() + 10, row.right() - cover.right() - 20, row.height() - 20);
        QFont titleFont = option.font;
        titleFont.setWeight(QFont::DemiBold);
        painter->setFont(titleFont);
        painter->setPen(selected ? theme::accentText() : theme::text());
        painter->drawText(text, Qt::AlignLeft | Qt::AlignTop,
                          QFontMetrics(titleFont).elidedText(name, Qt::ElideRight, int(text.width())));
        painter->setFont(option.font);
        painter->setPen(theme::muted());
        painter->drawText(text, Qt::AlignLeft | Qt::AlignBottom,
                          QFontMetrics(option.font).elidedText(index.data(kSubtitleRole).toString(),
                                                               Qt::ElideRight, int(text.width())));
    }
    painter->restore();
}

SongDelegate::SongDelegate(CoverFor coverFor, QObject *parent)
    : QStyledItemDelegate(parent), m_coverFor(std::move(coverFor)) {}

QSize SongDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return {400, 56};
}

void SongDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                         const QModelIndex &index) const
{
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    const QRectF row = QRectF(option.rect).adjusted(0, 1, -4, -1);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (selected || hover) {
        QPainterPath bg;
        bg.addRoundedRect(row, 8, 8);
        painter->fillPath(bg, selected ? theme::cardSelected() : theme::cardHover());
    }
    // The number column: the song's number, a play mark under the mouse, and
    // a speaker on the song mpv is playing.
    const bool playing = index.data(kPlayingRole).toBool();
    const QRectF number(row.left(), row.top(), 44, row.height());
    if (playing || hover) {
        const QRectF mark(number.center().x() - 8, number.center().y() - 8, 16, 16);
        icons::paint(*painter, playing ? icons::Icon::Playing : icons::Icon::Play, mark,
                     playing ? theme::accent() : theme::text());
    } else {
        painter->setFont(option.font);
        painter->setPen(theme::muted());
        painter->drawText(number, Qt::AlignCenter, index.data(kNumberRole).toString());
    }

    const qreal side = row.height() - 12;
    const QRectF cover(number.right() + 4, row.top() + 6, side * 16 / 9, side);
    const QString track = index.data(Qt::DisplayRole).toString();
    paintCover(*painter, cover, m_coverFor(index), track, 5);

    // Right-hand columns: when the list was added, and the song's length.
    const QRectF duration(row.right() - kDurationColumn - 8, row.top(), kDurationColumn, row.height());
    const QRectF added(duration.left() - kAddedColumn, row.top(), kAddedColumn, row.height());
    const bool roomy = added.left() - cover.right() > 260;
    painter->setFont(option.font);
    painter->setPen(theme::muted());
    const QString length = index.data(kDurationRole).toString();
    painter->drawText(duration, Qt::AlignRight | Qt::AlignVCenter, length.isEmpty() ? QStringLiteral("–") : length);
    if (roomy)
        painter->drawText(added, Qt::AlignLeft | Qt::AlignVCenter, index.data(kAddedRole).toString());

    const qreal textRight = roomy ? added.left() - 16 : duration.left() - 16;
    const QRectF text(cover.right() + 14, row.top() + 8, textRight - cover.right() - 14, row.height() - 16);
    QFont titleFont = option.font;
    titleFont.setWeight(QFont::DemiBold);
    painter->setFont(titleFont);
    painter->setPen(playing ? theme::accent() : selected ? theme::accentText() : theme::text());
    painter->drawText(text, Qt::AlignLeft | Qt::AlignTop,
                      QFontMetrics(titleFont).elidedText(track, Qt::ElideRight, int(text.width())));
    painter->setFont(option.font);
    painter->setPen(theme::muted());
    painter->drawText(text, Qt::AlignLeft | Qt::AlignBottom,
                      QFontMetrics(option.font).elidedText(index.data(kArtistRole).toString(),
                                                           Qt::ElideRight, int(text.width())));
    painter->restore();
}

} // namespace tiles
