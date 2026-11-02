#pragma once

#include <QWidget>
#include <functional>

namespace icons { class IconButton; }
class QLabel;
class QPushButton;

// The tray icon's menu, drawn as the app draws its cards rather than as a
// Windows menu: what is playing in grey, the three playback buttons, then
// the actions in rows, with thin lines between the groups.
class TrayMenu : public QWidget {
public:
    explicit TrayMenu(QWidget *parent = nullptr);

    std::function<void()> onPrevious, onPlayPause, onNext;
    std::function<void()> onOpen, onCheckUpdates, onThanks, onQuit;
    std::function<void(bool on)> onKeepRunning;

    void setNowPlaying(const QString &title, const QString &artist, bool paused);
    void setKeepRunning(bool on);
    // Opened at the cursor, kept on the screen it is on.
    void popupAt(const QPoint &cursor);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QLabel *m_now;
    icons::IconButton *m_pause;
    QLabel *m_keepTick;
    bool m_keepRunning = false;
};
