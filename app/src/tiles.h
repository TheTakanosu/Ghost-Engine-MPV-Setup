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

// Playlist covers drawn as circles instead of rounded squares (Settings).
void setCircleCovers(bool circle);
// The corner radius a playlist cover of this size gets: `normal`, or half
// its side when covers are circles.
qreal playlistRadius(const QRectF &rect, qreal normal);

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

// A library folder's row carries its name here; its playlists, shown under
// it while it is open, are marked as inside one.
constexpr int kFolderRole = Qt::UserRole + 8;
constexpr int kInFolderRole = Qt::UserRole + 9;
constexpr int kOpenRole = Qt::UserRole + 10;     // a folder showing its playlists

// A playlist as a row in the library panel: small cover, name, count. In a
// narrow panel only the cover is left, as Spotify's library does; the dense
// view drops the covers and keeps one line per list.
class RowDelegate : public QStyledItemDelegate {
public:
    using CoversFor = std::function<QVector<QPixmap>(const QModelIndex &)>;

    RowDelegate(CoversFor coversFor, QObject *parent);
    void setCompact(bool compact) { m_compact = compact; }
    void setDense(bool dense) { m_dense = dense; }
    bool dense() const { return m_dense; }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    CoversFor m_coversFor;
    bool m_compact = false;
    bool m_dense = false;
};

// A song in a playlist page: number, cover, track and artist.
constexpr int kNumberRole = Qt::UserRole + 2;
constexpr int kArtistRole = Qt::UserRole + 3;
constexpr int kPlayingRole = Qt::UserRole + 4;   // the song mpv is on
constexpr int kAddedRole = Qt::UserRole + 5;     // "3 weeks ago"
constexpr int kDurationRole = Qt::UserRole + 6;  // "3:16", or empty
constexpr int kEntryRole = Qt::UserRole + 7;     // its place in the file, whatever the sort
constexpr int kAddedAtRole = Qt::UserRole + 11;  // when it was added, for sorting
constexpr int kKeyRole = Qt::UserRole + 12;      // a library row's key: file name, or "folder:<name>"
constexpr int kPinnedRole = Qt::UserRole + 13;
constexpr int kOfflineRole = Qt::UserRole + 14;  // a song kept on disk

// Widths of the right-hand columns, shared by the rows and their header.
constexpr int kAddedColumn = 150;
constexpr int kDurationColumn = 72;

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

// A video in a video list's page, as a card in YouTube's grid: the
// thumbnail with its length on it, the title on up to two lines, then who
// made it and when it was added.
class VideoCardDelegate : public QStyledItemDelegate {
public:
    using CoverFor = std::function<QPixmap(const QModelIndex &)>;

    VideoCardDelegate(CoverFor coverFor, QObject *parent);
    void setCardWidth(int width) { m_width = width; }
    QSize cardSize() const;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    CoverFor m_coverFor;
    int m_width = 300;
};

} // namespace tiles
