#pragma once

#include <QWidget>

#include <QPixmap>
#include <QStyledItemDelegate>
#include <QVector>
#include <functional>

class QPainter;

namespace tiles {

// The Liked songs mark (Batin, 2026-11-07/12): a ring with a + in it, or,
// liked, a disc in the accent with a tick. Drawn, not taken from the icon
// font, whose + sat half a pixel off the ring's centre. `ring` should be
// whole pixels and an odd size, so a 1.5 px stroke falls on the centre.
void paintLikeMark(QPainter &painter, const QRectF &ring, bool liked, bool hover);

// A soft shadow cast by an edge over what scrolls under it (2026-11-12): a
// strip laid over the page, the mouse going through, dark at the edge and
// clear at its far side. `down`: the edge is the strip's top (a header
// above the videos); else its bottom (a panel rising from below).
// `strength` 0..1 fades it in, as the content starts to pass under it.
class Shade : public QWidget {
public:
    explicit Shade(QWidget *parent, bool down);
    static int depth();   // the strip's height, at the zoom
    void setStrength(qreal strength);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    bool m_down;
    qreal m_strength = 0;
};

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

// A cover's dominant colour: the hue most of its lively pixels share,
// averaged; the plain average for a grey picture, invalid for none.
QColor coverColor(const QPixmap &cover);

// A radio's cover: the Ghost Engine ghost, white, on a square washed in
// `base` (the first song's cover colour, or the accent). Unrounded.
QPixmap radioCover(int side, const QColor &base);

// Liked songs' own cover, as Spotify's: a white heart on a wash of the
// accent. Unrounded.
QPixmap likedCover(int side, const QColor &accent);

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
constexpr int kMatchRole = Qt::UserRole + 15;    // a list found by a song in it: the search

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
constexpr int kLikedRole = Qt::UserRole + 17;    // a song in Liked songs
constexpr int kAvatarRole = Qt::UserRole + 16;   // a YouTube card: its channel's picture URL
constexpr int kHiddenRole = Qt::UserRole + 18;   // a YouTube card put away: "Show less" of its channel

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
    // The + (or tick) left of the length: a click there likes the song.
    static QRectF likeRect(const QRect &rowRect);

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
    std::function<QString(const QModelIndex &)> channelFor;
    // The channel's round picture left of the title (YouTube's grid); left
    // unset, the text takes the card's whole width as before.
    std::function<QPixmap(const QModelIndex &)> avatarFor;
    void setCardWidth(int width) { m_width = width; }
    QSize cardSize() const;
    // A ⋯ right of the title on hover, opening the card's menu (YouTube's).
    bool menuDots = false;
    // Where those are in a card's item rectangle: the ⋯, and on a card put
    // away (kHiddenRole) its Undo.
    static QRectF dotsRect(const QRectF &item);
    static QRectF undoRect(const QRectF &item);

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    CoverFor m_coverFor;
    int m_width = 300;
};

} // namespace tiles
