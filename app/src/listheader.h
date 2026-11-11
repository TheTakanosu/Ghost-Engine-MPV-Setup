#pragma once

#include <QColor>
#include <QPixmap>
#include <QVector>
#include <QWidget>

#include <functional>

class QHBoxLayout;
class QLabel;
class QScrollBar;

// The top of a playlist page: its collage, and behind it a wash of the
// covers' own colour fading into the page, the way Spotify tints a playlist.
// The library's list pages and the search page's (a radio, a YouTube Music
// playlist) both use it, and fold it the same way as their songs scroll.
class PlaylistHeader : public QWidget {
public:
    using Source = std::function<QVector<QPixmap>()>;
    explicit PlaylistHeader(Source source, QWidget *parent = nullptr);
    void setName(const QString &name) { m_name = name; update(); }
    // A colour the user chose for this list; invalid follows the cover.
    void setTint(const QColor &tint) { m_tint = tint; update(); }
    // The cover fills the header's height less its margins, so shrinking the
    // header on scroll shrinks the cover with it.
    QRect coverRect() const;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Source m_source;
    QString m_name;
    QColor m_tint;
};

namespace listheader {

// The header's height at the top of the songs and once they are scrolled,
// at the zoom.
int tall();
int folded();
// Zoomed in a short window the header took most of the page and the songs
// had no room under it: at most half the page's height.
int tallFor(int pageHeight);
// How far folded it is (0 open, 1 folded) for the songs' scroll position.
qreal foldFor(int scrolled);
// The header's height `fold` of the way from open to folded.
int heightFor(qreal fold, int tallNow);
// How far folded a header of this height is.
qreal foldOf(const PlaylistHeader *header, int tallNow);
// Lays out the text beside the cover for the header's height: the kicker
// shows only tall, the info line only above the slimmest bar.
void arrange(PlaylistHeader *header, QHBoxLayout *row, QLabel *kicker, QLabel *info);
// The largest size the name fits in `available` at, from 40 pt (open) down
// to a floor; `elided` gets the name cut with an ellipsis if even that is
// too wide.
qreal titlePoints(const QFont &base, const QString &name, int available, qreal fold, QString *elided);

// "#  Title · Artist ... (clock)" over a page's songs, without sorting (the
// library's list page has its own, whose names sort). Same look.
QWidget *plainColumns();

} // namespace listheader
