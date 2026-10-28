#pragma once

#include <QPixmap>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVector>
#include <QWidget>
#include <functional>

class QLabel;
class QListWidget;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTextBrowser;

// The page a video list plays on, laid out like YouTube's watch page: the
// player large on the left with its own controls, the title, channel and
// description under it, the list's videos down the right. The picture is
// mpv itself, drawing into surface() (started with --wid), so quality and
// speed are those of its own window; the controls under it are the app's,
// and stay under the picture in full screen too.
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
    void setDescription(const QString &text);
    // What mpv says: position and length in seconds, paused or not, its
    // tracks (for the subtitles menu), the height cap and the speed.
    void setTimes(double position, double duration);
    void setPaused(bool paused);
    void setTracks(const QVariantList &tracks);
    void setQualityCap(int height);   // 0: best
    void setSpeed(double speed);
    // mpv was moved to its own window: the player area says so instead.
    void setPoppedOut(bool out);
    // Only the player and its controls, filling the screen; the controls
    // hide when the mouse rests.
    void setFullScreen(bool full);
    void refreshCovers() const;

    std::function<QPixmap(const QString &target)> coverFor;
    std::function<QString(const QString &target)> lengthFor;
    std::function<void(const QString &target)> onPick;
    std::function<void()> onPopOut;
    std::function<void()> onBringBack;
    std::function<void()> onFullScreen;
    std::function<void()> onTogglePause;
    std::function<void(double seconds)> onSeek;      // to this second
    std::function<void(double seconds)> onSkip;      // by this many
    std::function<void(const QVariant &sid)> onSubtitle;   // a track id, or "no"
    std::function<void(int height)> onQuality;       // 0: best
    std::function<void(double speed)> onSpeed;

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    void fitPlayer();
    QWidget *buildControls();
    void showControls();

    QWidget *m_surface;
    QWidget *m_away;            // shown in the player's place while mpv is popped out
    QStackedWidget *m_player;
    QWidget *m_controls;
    QWidget *m_info;
    QWidget *m_side;
    QLabel *m_title;
    QLabel *m_channel;
    QLabel *m_listName;
    QPushButton *m_youtube;
    QTextBrowser *m_description;
    QPushButton *m_more;
    QListWidget *m_list;
    QPushButton *m_play;
    QSlider *m_seek;
    QLabel *m_time;
    QPushButton *m_subs;
    QPushButton *m_quality;
    QPushButton *m_speed;
    QVariantList m_tracks;
    int m_cap = 0;
    double m_speedNow = 1;
    double m_duration = 0;
    bool m_dragging = false;
    QString m_link;
    bool m_full = false;
    bool m_expanded = false;
    QTimer m_idle;              // full screen: hides the controls when the mouse rests
    QPoint m_lastMouse;
};
