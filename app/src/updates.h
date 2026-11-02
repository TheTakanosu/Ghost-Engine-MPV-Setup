#pragma once

#include <QDialog>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QString>
#include <functional>

class QAbstractButton;
class QLabel;
class QPlainTextEdit;
class QProcess;
class QPushButton;

// Updates come from update.ps1 next to mpv.exe, the same script update.bat
// runs: it downloads, checks every file against its SHA-256 and replaces
// nothing on a mismatch. The app only asks it what is behind (-Check) and
// which parts to update (-Only), so there is one way to update, not two.
namespace updates {

struct Part {
    QString key;         // ghost, ytdlp, mpv, node: what -Only takes
    QString name;        // as shown
    QString installed;
    QString latest;
    bool newer = false;
    QString notes;       // the release page, for Ghost Engine
};

QString script();        // update.ps1 beside mpv.exe; empty when there is none

} // namespace updates

// Runs update.ps1 -Check in the background, hidden; at most one at a time.
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    void check();
    bool running() const { return m_process != nullptr; }
    const QList<updates::Part> &parts() const { return m_parts; }
    int newerCount() const;

signals:
    // ok is false when the script is missing or said nothing readable.
    void finished(bool ok);

private:
    QProcess *m_process = nullptr;
    QList<updates::Part> m_parts;
};

// The parts, each with a box to tick: what is installed, what is out. Ghost
// Engine and yt-dlp come ticked (yt-dlp is what breaks when YouTube changes),
// mpv and node are left to the user. Updating the app itself closes it: the
// script then runs in its own window and opens the app again at the end.
class UpdateDialog : public QDialog {
public:
    UpdateDialog(const QList<updates::Part> &parts, QWidget *parent = nullptr);

    // Told before the app is closed for its own update, and before mpv must
    // go (an mpv update replaces mpv.exe).
    std::function<void()> onQuitForUpdate;
    std::function<void()> onStopPlayer;

protected:
    // Frameless and rounded like the rest of the app, dragged by any spot
    // that is not a control.
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void start();
    QPoint m_dragFrom;
    QList<QPair<updates::Part, QAbstractButton *>> m_rows;
    QLabel *m_status;
    QPlainTextEdit *m_log;
    QPushButton *m_update;
    QPushButton *m_later;
    QProcess *m_process = nullptr;
};
