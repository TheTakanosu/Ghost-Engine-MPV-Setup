#pragma once

#include <QPixmap>
#include <QString>
#include <QVector>
#include <QWidget>
#include <functional>

class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;

// The page a video list plays on, laid out like YouTube's watch page: the
// player large on the left with the title and channel under it, the list's
// videos down the right. The player is mpv itself, drawing into surface()
// (started with --wid), so the picture, the quality and mpv's own controls
// are the same as in its window.
class VideoPage : public QWidget {
public:
    explicit VideoPage(QWidget *parent = nullptr);

    // The native window mpv draws into. Created at once and kept, so its
    // handle stays valid for as long as the app runs.
    QWidget *surface() const { return m_surface; }

    struct Entry {
        QString title;
        QString target;   // the playlist line, as mpv's "path" has it
    };
    void setList(const QString &name, const QVector<Entry> &entries);
    void setPlaying(const QString &target, const QString &title, const QString &channel, const QString &link);
    // mpv was moved to its own window: the player area says so instead.
    void setPoppedOut(bool out);
    // Only the player, filling the window.
    void setFullScreen(bool full);
    void refreshCovers() const;

    std::function<QPixmap(const QString &target)> coverFor;
    std::function<QString(const QString &target)> lengthFor;
    std::function<void(const QString &target)> onPick;
    std::function<void()> onPopOut;
    std::function<void()> onBringBack;
    std::function<void()> onFullScreen;

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    void fitPlayer();

    QWidget *m_surface;
    QWidget *m_away;            // shown in the player's place while mpv is popped out
    QStackedWidget *m_player;
    QWidget *m_info;
    QWidget *m_side;
    QLabel *m_title;
    QLabel *m_channel;
    QLabel *m_listName;
    QPushButton *m_youtube;
    QListWidget *m_list;
    QString m_link;
    bool m_full = false;
};
