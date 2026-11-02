#pragma once

#include <QAbstractButton>
#include <QPixmap>
#include <QString>
#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QSlider;
class QTimer;
class ZoomBar;

// How much memory the player and the app may keep, from Settings > Performance.
namespace performance {

struct Level {
    const char *name;
    const char *ahead;      // demuxer-max-bytes: video loaded ahead of where it plays
    const char *back;       // demuxer-max-back-bytes: kept behind it, to seek back without loading again
    int aheadSecs;          // demuxer-readahead-secs
    int coversMb;           // covers the app keeps decoded
};
const Level &level(int index);    // 0 Low, 1 Balanced, 2 High
constexpr int kLevels = 3;
constexpr int kDefault = 1;

} // namespace performance

// mpv.conf, read and changed one setting at a time. Lines the app does not
// manage are left exactly as they are, and conf-merge.ps1 keeps the user's
// values through updates, so a setting made here survives one.
namespace mpvconf {

QString path();                                   // mpv.conf next to mpv.exe
QString value(const QString &key);                // "key=value" lines; empty if unset
bool setValue(const QString &key, const QString &value);
// "script-opts-append=<name>=<value>" lines, read by the Ghost Engine scripts.
QString option(const QString &name);
bool setOption(const QString &name, const QString &value);   // empty removes it

} // namespace mpvconf

// An on/off switch drawn as a pill with a knob, as in the rest of the app.
class ToggleSwitch : public QAbstractButton {
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {44, 24}; }

protected:
    void paintEvent(QPaintEvent *) override;
};

// The Settings page: playback quality, the player window, and the look. Each
// change is saved the moment it is made; there is no Apply button.
class SettingsPage : public QWidget {
public:
    explicit SettingsPage(QWidget *parent = nullptr);

    // Told when a setting the rest of the app shows changes.
    std::function<void()> onLookChanged;
    // Rich Presence on Discord, for songs and for videos apart.
    std::function<void(bool songs, bool videos)> onPresenceChanged;
    std::function<void(const QString &format)> onVideoFormatChanged;
    std::function<void(bool shown)> onPlayerWindowChanged;
    // Downloads: a line on what is kept, and clearing all of it.
    std::function<QString()> downloadsSummary;
    std::function<void()> onRemoveDownloads;
    // Each downloaded list, shown with its cover and what it takes.
    struct DownloadedList {
        QString path;
        QString name;
        QPixmap cover;
        int songs = 0;
        qint64 bytes = 0;
        int toGo = 0;
    };
    std::function<QList<DownloadedList>()> downloadedLists;
    std::function<void(const QString &path)> onRemoveListDownload;
    // Appearance: the zoom picked on the bar (the same as Ctrl + / Ctrl -).
    std::function<void(qreal zoom)> onZoomChanged;
    // Performance: a level picked, and what the app and its player use now.
    std::function<void(int level)> onMemoryChanged;
    std::function<QString()> memoryUsage;
    // YouTube sign-in: the browser yt-dlp reads cookies from (empty: none).
    std::function<void(const QString &browser)> onCookiesBrowserChanged;

    // The zoom changed elsewhere (Ctrl + wheel): the bar follows.
    void showZoom(qreal zoom);

    // Read again from mpv.conf and the app's settings, when the page opens.
    void load();

private:
    QComboBox *m_video;
    ToggleSwitch *m_autoQuality;
    QComboBox *m_audio;
    QSlider *m_volume;
    QLabel *m_volumeValue;
    ToggleSwitch *m_window;
    ToggleSwitch *m_embedVideo;
    QComboBox *m_theme;
    QComboBox *m_covers;
    ToggleSwitch *m_volumeNumber;
    ToggleSwitch *m_presenceSongs;
    ToggleSwitch *m_presenceVideos;
    QLabel *m_confPath;
    QComboBox *m_offlineQuality;
    QLabel *m_offlineUsed;
    QWidget *m_listsBox;
    ZoomBar *m_zoom;
    QComboBox *m_memory;
    QLabel *m_memoryNow;
    QLabel *m_cookiesFile;
    ToggleSwitch *m_cookiesBrowser;
    QComboBox *m_browser;
    void saveCookiesBrowser();
    QTimer *m_memoryTimer;
    bool m_loading = false;

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
};
