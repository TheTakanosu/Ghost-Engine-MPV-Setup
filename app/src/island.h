#pragma once

// The floating island in the middle of the top bar (Batın's idea,
// 2026-11-08): while a YouTube video plays and its page is not open, a pill
// says what is playing; a click goes to the video, the pointer resting on it
// opens it downward into a card with the picture, the channel and buttons.
// It replaces the corner player that came up by itself.

#include <QPair>
#include <QWidget>
#include <functional>

class MpvClient;
class ThumbnailCache;
class QVariantAnimation;
class QTimer;
namespace icons { class IconButton; }
class QPushButton;

class NowIsland : public QWidget {
public:
    NowIsland(MpvClient *mpv, ThumbnailCache *covers, QWidget *parent);

    std::function<bool()> shouldShow;   // a video playing whose page is not open
    std::function<void()> onOpen;       // to the video's page
    std::function<void()> onMini;       // the corner player instead
    // The free stretch of the top bar, left and right x in the window: the
    // pill sits in its middle and no wider (it lay over the search box in a
    // narrow window, Impeccable review B1, 2026-11-10).
    std::function<QPair<int, int>()> freeSpan;

    // Shows or hides it and lays it out again: on a new video, a page
    // change, a resize of the window.
    void refresh();

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setOpen(bool open);
    void place();
    QRectF body() const;

    MpvClient *m_mpv;
    ThumbnailCache *m_covers;
    QVariantAnimation *m_anim;
    qreal m_open = 0;      // 0 the pill, 1 the card
    bool m_wantOpen = false;
    QTimer *m_hover;
    QString m_title, m_channel, m_videoId;
    icons::IconButton *m_play;
    QPushButton *m_openButton;
    QPushButton *m_miniButton;
};
