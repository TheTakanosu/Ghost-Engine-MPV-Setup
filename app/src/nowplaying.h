#pragma once

#include <QWidget>
#include <functional>

class MpvClient;
class ThumbnailCache;
class QAbstractButton;
class QLabel;
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
    void refreshPosition();
    void setVolumeNumber(bool shown);

signals:
    // The cover was clicked: show the list mpv is playing.
    void openPlaying();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onProperty(const QString &name);
    void showIdle();

    MpvClient *m_mpv;
    ThumbnailCache *m_covers;
    QLabel *m_cover;
    QLabel *m_title;
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
    QLabel *m_volumeNumber;
    QAbstractButton *m_speaker;
    bool m_dragging = false;
};
