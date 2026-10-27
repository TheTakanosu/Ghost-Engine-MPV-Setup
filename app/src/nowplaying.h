#pragma once

#include <QWidget>

class MpvClient;
class ThumbnailCache;
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
    QSlider *m_position;
    QSlider *m_volume;
    bool m_dragging = false;
};
