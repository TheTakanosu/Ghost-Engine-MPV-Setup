#pragma once

#include "ytmusic.h"

#include <QWidget>
#include <functional>

class QLabel;
class QPushButton;
class QWheelEvent;
class RemotePictures;

// The top of a channel's page, as YouTube's (Batın, 2026-11-10): its banner,
// its picture, name, handle and counts, the start of its description (more:
// all of it, the links, when it started) and Follow. Scrolled down it folds
// to one row so the videos have the room.
class ChannelHeader : public QWidget {
    Q_OBJECT

public:
    ChannelHeader(RemotePictures *pictures, QWidget *parent = nullptr);

    // A new channel: its name until the page answers.
    void reset(const QString &channelId, const QString &name);
    void setPage(const ytmusic::ChannelPage &page);
    void setAbout(const ytmusic::ChannelAbout &about);
    void setFollowed(bool followed);
    void setLive(bool live);
    // Whole or folded at once (the scrolling folds it bit by bit).
    void setFolded(bool folded, bool animate = false);
    bool folded() const { return m_folded; }
    // How far the videos under it are scrolled: it shrinks by as much.
    void setScrolled(int scrolled);
    QString channelId() const { return m_id; }

    std::function<void(bool follow)> onFollow;
    std::function<void(QWheelEvent *)> onWheel;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void refreshPictures();
    void showMore(bool more);
    void placeBanner();
    void measure();
    void remeasure();
    void placeGap();

    RemotePictures *m_pictures;
    QString m_id;
    QString m_bannerUrl;
    QString m_avatarUrl;
    bool m_folded = false;
    int m_openHeight = 0;
    int m_foldedHeight = 0;
    bool m_more = false;
    bool m_hasBanner = false;
    bool m_canMore = false;   // the page gave a way to its About
    ytmusic::ChannelAbout m_about;
    bool m_aboutKnown = false;
    QString m_preview;

    class Banner *m_banner = nullptr;
    class RoundPicture *m_avatar = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_meta = nullptr;
    QLabel *m_description = nullptr;
    class QScrollArea *m_descriptionBox = nullptr;
    QPushButton *m_moreButton = nullptr;
    QLabel *m_links = nullptr;
    QLabel *m_facts = nullptr;
    QLabel *m_liveTag = nullptr;
    QPushButton *m_follow = nullptr;
    QWidget *m_body = nullptr;   // the whole header, one block
    QWidget *m_row = nullptr;    // folded: the one row
    class RoundPicture *m_smallAvatar = nullptr;
    QLabel *m_smallName = nullptr;
    class QSpacerItem *m_avatarNudge = nullptr;
    QLabel *m_smallMeta = nullptr;
    QPushButton *m_smallFollow = nullptr;
    class QGraphicsOpacityEffect *m_bodyFade = nullptr;
    class QGraphicsOpacityEffect *m_rowFade = nullptr;
    QWidget *m_gap = nullptr;   // the shade under it, over the videos
    int m_scrolled = 0;
    bool m_inResize = false;
};
