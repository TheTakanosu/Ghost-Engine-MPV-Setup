#include "tiles.h"

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

} // namespace tiles
