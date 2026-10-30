#pragma once

#include <QDialog>
#include <QList>
#include <QPair>
#include <QPixmap>
#include <QString>

class QGridLayout;
class QLabel;
class QPushButton;

namespace songinfo {

// What YouTube knows about a song, with the noise of a description (links,
// "subscribe", lyrics, hashtags) left out.
struct Facts {
    QString id;
    QString album;
    QString released;   // "Mar 4, 2021", or a year
    QString label;      // "Provided to YouTube by ..." or the ℗ line
    QList<QPair<QString, QString>> credits;   // "Composer", "Name"
    QString about;      // a few plain lines from the description, or empty
    bool isEmpty() const { return album.isEmpty() && released.isEmpty() && label.isEmpty() && credits.isEmpty() && about.isEmpty(); }
};

// The yt-dlp arguments that print what parse() reads.
QStringList ytDlpArguments(const QString &url);
Facts parse(const QString &printed);

}

// Right-click a song, "About this song": its cover, names and what the
// YouTube page says about it, filled in when yt-dlp answers.
class SongInfoDialog : public QDialog {
public:
    SongInfoDialog(const QString &title, const QString &artist, QWidget *parent = nullptr);
    void setCover(const QPixmap &cover);
    void setFacts(const songinfo::Facts &facts);
    void setFailed();

private:
    void addRow(const QString &name, const QString &value);
    QLabel *m_cover = nullptr;
    QLabel *m_status = nullptr;
    QGridLayout *m_rows = nullptr;
    QLabel *m_creditsTitle = nullptr;
    QGridLayout *m_credits = nullptr;
    QLabel *m_aboutTitle = nullptr;
    QLabel *m_about = nullptr;
    QPushButton *m_youtube = nullptr;
    QString m_id;
};
