#include "tiles.h"

#include "icons.h"
#include "theme.h"

#include <QPainter>
#include <QPainterPath>

namespace tiles {

namespace {

// The pin of a pinned row, from the Windows icon font, before its subtitle;
// returns how far the subtitle moves over.
qreal paintPin(QPainter *painter, const QRectF &text, Qt::Alignment vertical)
{
    QFont f(QStringLiteral("Segoe Fluent Icons"));
    f.setPixelSize(11);
    painter->save();
    painter->setFont(f);
    painter->setPen(theme::accent());
    painter->drawText(text, Qt::AlignLeft | vertical, QString(QChar(0xE718)));
    painter->restore();
    return 16;
}

} // namespace

void paintCover(QPainter &painter, const QRectF &rect, const QPixmap &pixmap,
                const QString &name, qreal radius)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(rect, radius, radius);
    // Within any clip already set: a collage's cells stay inside its
    // rounded (or round) outline instead of replacing it with square ones.
    painter.setClipPath(clip, Qt::IntersectClip);

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

namespace {
bool g_circle = false;
} // namespace

void setCircleCovers(bool circle)
{
    g_circle = circle;
}

qreal playlistRadius(const QRectF &rect, qreal normal)
{
    return g_circle ? qMin(rect.width(), rect.height()) / 2 : normal;
}

void paintCollage(QPainter &painter, const QRectF &rect, const QVector<QPixmap> &covers,
                  const QString &name, qreal radius)
{
    radius = playlistRadius(rect, radius);
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
    const int s = int(m_size * theme::zoom());
    return {s + 24, s + int(70 * theme::zoom())};
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
    const int h = int((m_dense && !m_compact ? 34 : 64) * theme::zoom());
    return {m_compact ? h : 240, h};
}

void RowDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                        const QModelIndex &index) const
{
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    const QRectF row = QRectF(option.rect).adjusted(2, 2, -2, -2);
    const bool lined = m_dense && !m_compact && index.data(kInFolderRole).toBool();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (selected || hover) {
        QPainterPath bg;
        bg.addRoundedRect(lined ? row.adjusted(26, 0, 0, 0) : row, 8, 8);
        painter->fillPath(bg, selected ? theme::cardSelected() : theme::cardHover());
    }
    const QString name = index.data(Qt::DisplayRole).toString();
    const bool folder = !index.data(kFolderRole).toString().isEmpty();
    const qreal indent = index.data(kInFolderRole).toBool() && !m_compact ? (m_dense ? 30 : 18) : 0;

    if (m_dense && !m_compact) {
        // One line: a folder's mark or nothing, the name, then what it is.
        // A thin line down the left of a folder's lists ties them to it.
        if (indent > 0) {
            painter->setPen(QPen(QColor(70, 68, 84), 1.5));
            painter->drawLine(QPointF(row.left() + 18, row.top() - 2), QPointF(row.left() + 18, row.bottom() + 2));
        }
        qreal x = row.left() + 10 + indent;
        if (folder) {
            icons::paint(*painter, icons::Icon::Folder, QRectF(x, row.center().y() - 8, 16, 16),
                         index.data(kOpenRole).toBool() ? theme::accent() : theme::muted());
            x += 24;
        }
        const QRectF text(x, row.top(), row.right() - x - 8, row.height());
        QFont nameFont = option.font;
        nameFont.setWeight(QFont::DemiBold);
        const QString kind = QStringLiteral("  ·  ") + index.data(kSubtitleRole).toString();
        const QFontMetrics nameMetrics(nameFont);
        const QString shownName = nameMetrics.elidedText(name, Qt::ElideRight, int(text.width()));
        painter->setFont(nameFont);
        painter->setPen(selected ? theme::accentText() : theme::text());
        painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter, shownName);
        qreal used = nameMetrics.horizontalAdvance(shownName);
        if (index.data(kPinnedRole).toBool())
            used += 8 + paintPin(painter, text.adjusted(used + 8, 0, 0, 0), Qt::AlignVCenter) - 8;
        painter->setFont(option.font);
        painter->setPen(theme::muted());
        painter->drawText(text.adjusted(used, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(option.font).elidedText(kind, Qt::ElideRight, int(text.width() - used)));
        painter->restore();
        return;
    }

    const qreal side = row.height() - 12;
    const QRectF cover(m_compact ? row.center().x() - side / 2 : row.left() + 6 + indent, row.top() + 6, side, side);
    if (folder) {
        QPainterPath tile;
        tile.addRoundedRect(cover, playlistRadius(cover, 6), playlistRadius(cover, 6));
        painter->fillPath(tile, QColor(40, 39, 50));
        const qreal mark = side * 0.42;
        icons::paint(*painter, icons::Icon::Folder,
                     QRectF(cover.center().x() - mark / 2, cover.center().y() - mark / 2, mark, mark),
                     index.data(kOpenRole).toBool() ? theme::accent() : theme::text());
    } else {
        paintCollage(*painter, cover, m_coversFor(index), name, 6);
    }

    if (!m_compact) {
        const QRectF text(cover.right() + 12, row.top() + 10, row.right() - cover.right() - 20, row.height() - 20);
        QFont titleFont = option.font;
        titleFont.setWeight(QFont::DemiBold);
        painter->setFont(titleFont);
        painter->setPen(selected ? theme::accentText() : theme::text());
        painter->drawText(text, Qt::AlignLeft | Qt::AlignTop,
                          QFontMetrics(titleFont).elidedText(name, Qt::ElideRight, int(text.width())));
        QRectF sub = text;
        if (index.data(kPinnedRole).toBool())
            sub.setLeft(sub.left() + paintPin(painter, text, Qt::AlignBottom));
        painter->setFont(option.font);
        painter->setPen(theme::muted());
        painter->drawText(sub, Qt::AlignLeft | Qt::AlignBottom,
                          QFontMetrics(option.font).elidedText(index.data(kSubtitleRole).toString(),
                                                               Qt::ElideRight, int(sub.width())));
    }
    painter->restore();
}

SongDelegate::SongDelegate(CoverFor coverFor, QObject *parent)
    : QStyledItemDelegate(parent), m_coverFor(std::move(coverFor)) {}

QSize SongDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return {400, int(56 * theme::zoom())};
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
    QRectF artistLine = text;
    if (index.data(kOfflineRole).toBool()) {
        QFont mark(QStringLiteral("Segoe Fluent Icons"));
        mark.setPixelSize(int(12 * theme::zoom()));
        painter->setFont(mark);
        painter->setPen(theme::accent());
        painter->drawText(text, Qt::AlignLeft | Qt::AlignBottom, QString(QChar(0xE896)));   // download
        artistLine.setLeft(text.left() + 18 * theme::zoom());
    }
    painter->setFont(option.font);
    painter->setPen(theme::muted());
    painter->drawText(artistLine, Qt::AlignLeft | Qt::AlignBottom,
                      QFontMetrics(option.font).elidedText(index.data(kArtistRole).toString(),
                                                           Qt::ElideRight, int(artistLine.width())));
    painter->restore();
}

VideoCardDelegate::VideoCardDelegate(CoverFor coverFor, QObject *parent)
    : QStyledItemDelegate(parent), m_coverFor(std::move(coverFor))
{
}

QSize VideoCardDelegate::cardSize() const
{
    // The thumbnail at 16:9, then room for two lines of title and one more.
    const int text = int(78 * theme::zoom());
    return {m_width, m_width * 9 / 16 + text};
}

QSize VideoCardDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return cardSize();
}

void VideoCardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(option.rect).adjusted(8, 8, -8, -4);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    const bool playing = index.data(kPlayingRole).toBool();
    if (selected || hover) {
        QPainterPath bg;
        bg.addRoundedRect(card.adjusted(-6, -6, 6, 2), 14, 14);
        painter->fillPath(bg, selected ? theme::cardSelected() : theme::cardHover());
    }
    const QRectF thumb(card.left(), card.top(), card.width(), card.width() * 9 / 16);
    paintCover(*painter, thumb, m_coverFor ? m_coverFor(index) : QPixmap(), index.data(Qt::DisplayRole).toString(), 12);
    if (playing) {
        QPainterPath ring;
        ring.addRoundedRect(thumb.adjusted(1, 1, -1, -1), 12, 12);
        painter->setPen(QPen(theme::accent(), 3));
        painter->drawPath(ring);
    }

    QFont small = option.font;
    small.setPointSizeF(small.pointSizeF() * 0.85);
    small.setWeight(QFont::DemiBold);
    const QString length = index.data(kDurationRole).toString();
    if (!length.isEmpty() || playing) {
        const QString badgeText = playing ? QStringLiteral("Now playing") : length;
        const QFontMetricsF fm(small);
        const QRectF badge(thumb.right() - fm.horizontalAdvance(badgeText) - 16, thumb.bottom() - fm.height() - 10,
                           fm.horizontalAdvance(badgeText) + 10, fm.height() + 4);
        QPainterPath b;
        b.addRoundedRect(badge, 5, 5);
        painter->fillPath(b, playing ? theme::accent() : QColor(0, 0, 0, 200));
        painter->setFont(small);
        painter->setPen(playing ? theme::accentText() : Qt::white);
        painter->drawText(badge, Qt::AlignCenter, badgeText);
    }

    QFont title = option.font;
    title.setPointSizeF(title.pointSizeF() * 1.05);
    title.setWeight(QFont::DemiBold);
    const QFontMetricsF tm(title);
    const QRectF titleBox(card.left() + 2, thumb.bottom() + 8, card.width() - 4, tm.lineSpacing() * 2);
    painter->setFont(title);
    painter->setPen(playing ? theme::accent() : theme::text());
    // Two lines at most, the second cut with an ellipsis.
    QString text = index.data(Qt::DisplayRole).toString();
    const QString artist = index.data(kArtistRole).toString();
    if (!artist.isEmpty())
        text = artist + QStringLiteral(" - ") + text;
    int fit = 0;
    for (int i = text.size(); i > 0; --i)
        if (tm.horizontalAdvance(text.left(i)) <= titleBox.width()) { fit = i; break; }
    int cut = fit < text.size() ? int(text.left(fit).lastIndexOf(' ')) : fit;
    if (cut <= 0)
        cut = fit;
    painter->drawText(QRectF(titleBox.left(), titleBox.top(), titleBox.width(), tm.lineSpacing()),
                      Qt::AlignLeft | Qt::AlignVCenter, text.left(cut).trimmed());
    if (cut < text.size())
        painter->drawText(QRectF(titleBox.left(), titleBox.top() + tm.lineSpacing(), titleBox.width(), tm.lineSpacing()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          tm.elidedText(text.mid(cut).trimmed(), Qt::ElideRight, titleBox.width()));

    painter->setFont(small);
    painter->setPen(QColor(154, 152, 166));
    // The channel, then when it was added, as YouTube's cards have them.
    QStringList parts;
    if (channelFor) {
        const QString channel = channelFor(index);
        if (!channel.isEmpty())
            parts << channel;
    }
    if (index.data(kOfflineRole).toBool())
        parts << QStringLiteral("Downloaded");
    if (!index.data(kAddedRole).toString().isEmpty())
        parts << index.data(kAddedRole).toString();
    const QString under = QFontMetricsF(small).elidedText(parts.join(QStringLiteral("  ·  ")), Qt::ElideRight, titleBox.width());
    painter->drawText(QRectF(titleBox.left(), titleBox.bottom() + 2, titleBox.width(), QFontMetricsF(small).lineSpacing()),
                      Qt::AlignLeft | Qt::AlignVCenter, under);
    painter->restore();
}

} // namespace tiles
