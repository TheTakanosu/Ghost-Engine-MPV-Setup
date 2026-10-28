#pragma once

#include <QAbstractButton>
#include <QPixmap>
#include <QString>
#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QSlider;

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
    QComboBox *m_covers;
    ToggleSwitch *m_volumeNumber;
    QLabel *m_confPath;
    QComboBox *m_offlineQuality;
    QLabel *m_offlineUsed;
    QWidget *m_listsBox;
    bool m_loading = false;
};
