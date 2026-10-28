#pragma once

#include <QPixmap>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QWidget>
#include <functional>

class QGridLayout;
class QLabel;
class QAbstractScrollArea;
class QScrollArea;
class QListWidget;
class QPushButton;
class QSlider;
class QTextBrowser;
namespace icons { class IconButton; }

// The window mpv draws into (it is started with --wid). A plain Windows
// window, not a Qt widget: a native Qt widget made Qt turn the widgets
// around it native too, and the window's top bar then took the clicks that
// drag the window. This one Qt never sees; it is placed, cut and shown here.
class NativeSurface {
public:
    explicit NativeSurface(QWidget *window);
    WId winId() const { return WId(m_hwnd); }
    // Into this window (the main window or the mini player), at this place
    // in its client area, in Qt's pixels.
    void place(QWidget *window, const QRect &rect);
    QRect geometry() const { return m_rect; }
    void setCorner(int radius);
    void setVisibleArea(const QRect &area);   // its own coordinates; empty: all
    void setVisible(bool visible);
    // Back on top of its siblings: Qt opens a child window of its own over
    // the whole window once the video page shows, which would cover it.
    void keepOnTop();

private:
    void clip();
    void *m_hwnd = nullptr;
    QRect m_rect;
    qreal m_dpr = 1;
    int m_corner = 12;
    QRect m_visible;
};

// The page a video list plays on, laid out like YouTube's watch page: the
// player large on the left, the title, channel and description under it,
// the list's videos down the right. With the library collapsed it becomes a
// cinema: only the player, as large as the page lets it be.
//
// The picture is mpv itself, drawing into surface() (started with --wid).
// The surface is not part of the page: it is a native window over the main
// window, which the main window puts over screen() here, or small in a
// corner (the mini player) while another page is open. Native windows sit
// above everything the app paints, so the controls over the picture and the
// mini player's buttons are small windows of their own, kept on top of it.
class VideoPage : public QWidget {
public:
    explicit VideoPage(QWidget *parent = nullptr);

    // Made once the main window is set up, as a child of host; its handle
    // stays valid for as long as the app runs.
    void attachSurface(QWidget *host);
    NativeSurface *surface() const { return m_surface; }
    void setSurfaceCorner(int radius);
    void setSurfaceVisibleArea(const QRect &area);   // its own coordinates; empty: all
    // The window over the picture's visible part (global coordinates).
    void placeOverPicture(const QRect &picture, const QRect &visible);
    // The page's scrolling area, the part of the window the picture may show in.
    QWidget *viewport() const;
    QAbstractScrollArea *scrollArea() const;
    // Where the picture goes on this page.
    QWidget *screen() const { return m_screen; }
    // The see-through bar of controls laid over the picture, the page-coloured
    // corners laid over its square ones (a window region cannot be smoothed),
    // and the mini player: a small window of its own that the surface moves
    // into, with its buttons on top. All owned by the main window.
    QWidget *controls() const { return m_controls; }
    QWidget *corners() const { return m_corners; }
    QWidget *miniWindow() const { return m_miniWindow; }
    QWidget *mini() const { return m_mini; }
    void setOwner(QWidget *window);

    struct Entry {
        QString title;
        QString target;   // the playlist line, as mpv's "path" has it
    };
    void setList(const QString &name, const QVector<Entry> &entries);
    void setPlaying(const QString &target, const QString &title, const QString &channel, const QString &link);
    // The description, and a line over it such as "1,234 views · Oct 2, 2026".
    void setDescription(const QString &text, const QString &meta = {});
    // What mpv says: position and length in seconds, paused or not, its
    // tracks (for the subtitles menu), the height cap, the speed, the
    // volume and the picture's shape.
    void setTimes(double position, double duration);
    void setPaused(bool paused);
    bool paused() const { return m_paused; }
    void setTracks(const QVariantList &tracks);
    void setQualityCap(int height);   // 0: best
    void setSpeed(double speed);
    void setVolume(int volume, bool muted);
    void setAspect(double aspect);
    // mpv was moved to its own window: the player area says so instead.
    void setPoppedOut(bool out);
    // Only the player, filling the screen.
    void setFullScreen(bool full);
    // Only the player, as large as the page lets it be.
    void setCinema(bool cinema);
    void refreshCovers() const;

    std::function<QPixmap(const QString &target)> coverFor;
    std::function<QString(const QString &target)> lengthFor;
    std::function<QString(const QString &target)> channelFor;
    std::function<void(const QString &target)> onPick;
    std::function<void()> onPopOut;
    std::function<void()> onBringBack;
    std::function<void()> onFullScreen;
    std::function<void()> onTogglePause;
    std::function<void()> onPrevious;
    std::function<void()> onNext;
    std::function<void(double seconds)> onSeek;      // to this second
    std::function<void(double seconds)> onSkip;      // by this many
    std::function<void(const QVariant &sid)> onSubtitle;   // a track id, or "no"
    std::function<void(int height)> onQuality;       // 0: best
    std::function<void(double speed)> onSpeed;
    std::function<void(int volume)> onVolume;
    std::function<void()> onToggleMute;
    std::function<void()> onScreenMoved;   // screen() moved or changed size
    std::function<void()> onScrolled;      // the page scrolled: mpv's window must follow now
    std::function<void()> onMinimize;      // to the mini player
    std::function<void()> onRestore;       // the mini player was clicked
    std::function<void()> onCloseMini;
    std::function<void(const QPoint &at)> onMiniMoved;   // dragged there
    std::function<void(const QSize &size)> onMiniResized;

protected:
    void resizeEvent(QResizeEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void fitPlayer();
    void placeScreen();
    QWidget *buildControls();
    QWidget *buildMini();
    void setExpanded(bool expanded);
    void arrange();

    NativeSurface *m_surface = nullptr;
    QWidget *m_screen;          // the picture's place, under the surface
    QWidget *m_away;            // shown in its place while mpv is popped out
    QWidget *m_player;          // holds the screen, centred at the picture's shape
    QWidget *m_controls;
    QWidget *m_mini;
    QWidget *m_miniWindow;
    QWidget *m_corners;
    QGridLayout *m_grid;
    QScrollArea *m_page;
    QWidget *m_content;
    int m_under = 0;            // the height under the player, the description closed
    QWidget *m_info;
    QWidget *m_side;
    QLabel *m_title;
    QLabel *m_channel;
    QLabel *m_listName;
    QPushButton *m_youtube;
    QWidget *m_card;            // the description's box
    QLabel *m_meta;
    QTextBrowser *m_description;
    QPushButton *m_more;
    QListWidget *m_list;
    icons::IconButton *m_play;
    icons::IconButton *m_miniPlay;
    QSlider *m_seek;
    QLabel *m_time;
    QPushButton *m_mute;
    QSlider *m_volume;
    QPushButton *m_subs;
    QPushButton *m_quality;
    QPushButton *m_speed;
    QVariantList m_tracks;
    int m_cap = 0;
    double m_speedNow = 1;
    double m_duration = 0;
    double m_aspect = 16.0 / 9.0;
    bool m_dragging = false;
    bool m_paused = false;
    bool m_popped = false;
    QString m_link;
    bool m_full = false;
    bool m_cinema = false;
    bool m_expanded = false;
    bool m_hasDescription = false;
    bool m_pinned = false;      // the mini player kept above other programs
    QWidget *m_owner = nullptr; // the main window
public:
    bool miniPinned() const { return m_pinned; }
private:
};
