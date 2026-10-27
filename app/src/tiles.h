#pragma once

#include <QPixmap>
#include <QStyledItemDelegate>
#include <QVector>
#include <functional>

class QPainter;

namespace tiles {

// The playlist's index in the library, kept on each tile.
constexpr int kIndexRole = Qt::UserRole;
constexpr int kSubtitleRole = Qt::UserRole + 1;

// A cover filling `rect` (cropped, not stretched) with rounded corners, or a
// tinted block with the first letter while there is none.
void paintCover(QPainter &painter, const QRectF &rect, const QPixmap &pixmap,
                const QString &name, qreal radius);

// Up to four covers as a 2x2 square, the way music apps show a playlist. One
// cover fills the square; none falls back to the letter.
void paintCollage(QPainter &painter, const QRectF &rect, const QVector<QPixmap> &covers,
                  const QString &name, qreal radius);

// A playlist as a square tile with its name and count under it.
class TileDelegate : public QStyledItemDelegate {
public:
    using CoversFor = std::function<QVector<QPixmap>(const QModelIndex &)>;

    TileDelegate(int size, CoversFor coversFor, QObject *parent);

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    int m_size;
    CoversFor m_coversFor;
};

// A playlist as a row in the library panel: small cover, name, count. In a
// narrow panel only the cover is left, as Spotify's library does.
class RowDelegate : public QStyledItemDelegate {
public:
    using CoversFor = std::function<QVector<QPixmap>(const QModelIndex &)>;

    RowDelegate(CoversFor coversFor, QObject *parent);
    void setCompact(bool compact) { m_compact = compact; }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    CoversFor m_coversFor;
    bool m_compact = false;
};

// A song in a playlist page: number, cover, track and artist.
constexpr int kNumberRole = Qt::UserRole + 2;
constexpr int kArtistRole = Qt::UserRole + 3;

class SongDelegate : public QStyledItemDelegate {
public:
    using CoverFor = std::function<QPixmap(const QModelIndex &)>;

    SongDelegate(CoverFor coverFor, QObject *parent);

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    CoverFor m_coverFor;
};

} // namespace tiles
