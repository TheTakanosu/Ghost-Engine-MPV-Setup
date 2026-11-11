#pragma once

#include <QWidget>
#include <functional>

class MpvClient;
class ThumbnailCache;
class QAbstractButton;
class QLabel;
class QPushButton;
class QSlider;
namespace icons { class IconButton; }

// The strip along the bottom: what mpv is playing, its cover, the position
// and the controls. It drives mpv over IPC, so it works for an mpv started
// from Discord or Explorer as well as one started here.
class NowPlayingBar : public QWidget {
    Q_OBJECT

public:
    NowPlayingBar(MpvClient *mpv, ThumbnailCache *covers, QWidget *parent = nullptr);

    void refreshCover();
    // The next list mpv loads starts shuffled (or not); the app knows, mpv
    // does not say.
    void expectList(bool shuffled);
    // The playing song's number as the app shows it (row in the open list,
    // or its place in the file); 0 when it cannot tell.
    std::function<int()> numberFor;
    // The whole "N of M" (or "From queue"); empty: worked out from mpv's list.
    std::function<QString()> positionText;
    void refreshPosition();
    void setVolumeNumber(bool shown);
    // A keyboard shortcut pressing one of the bar's buttons, so a key does
    // just what the click does ("playPause", "shuffle", "repeat").
    void press(const QString &button);
    void setLyricsOpen(bool open);
    // A video (a YouTuber's, not a song) has no lyrics: the microphone hides.
    void setLyricsShown(bool shown);
    void setPanelOpen(bool open);
    // "Pick up where you left off": while nothing plays, the last song stands
    // here, paused at its second, and play starts it again (onResume). An
    // empty title takes it away.
    void setResume(const QString &title, const QString &subtitle, const QString &coverId, double at, double duration);
    std::function<void()> onResume;
    // Liked songs (Batın, 2026-11-11): the + beside the title adds the song
    // playing, a tick takes it out. canLike: a song is playing (not a
    // YouTuber's video); isLiked: it is in the list.
    std::function<bool()> canLike;
    std::function<bool()> isLiked;
    std::function<void()> onLike;
    std::function<void(const QPoint &global)> onLikeMenu;   // the + right-clicked
    void refreshLike();

signals:
    // The cover was clicked: show the list mpv is playing.
    void openPlaying();
    // The microphone was clicked: show or leave the lyrics.
    void toggleLyrics();
    // The now playing view at the right: shown or hidden.
    void togglePanel();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void onProperty(const QString &name);
    void showIdle();

    MpvClient *m_mpv;
    ThumbnailCache *m_covers;
    QLabel *m_cover;
    QLabel *m_title;
    QPushButton *m_like = nullptr;
    QWidget *m_leftBox = nullptr;
    int titleRoom() const;
    QLabel *m_subtitle;
    QLabel *m_elapsed;
    QLabel *m_total;
    icons::IconButton *m_playPause;
    icons::IconButton *m_shuffle;
    icons::IconButton *m_repeat;
    bool m_shuffled = false;
    int m_expect = -1;   // -1: no list on its way; else 0 or 1 for shuffled
    void showShuffle();
    void showRepeat();
    QSlider *m_position;
    QSlider *m_volume;
    QWidget *m_rightBox = nullptr;
    QLabel *m_volumeNumber;
    QAbstractButton *m_speaker;
    class QPushButton *m_lyrics;
    class QPushButton *m_panel;
    bool m_dragging = false;
    struct Resume { QString title, subtitle, coverId; double at = 0, duration = 0; } m_resume;
};
