#pragma once

#include <QHash>
#include <QImage>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

// A video's timeline pictures (Batin, 2026-11-20): YouTube's storyboard, a
// few sheets of small frames in a grid, one frame every couple of seconds.
// yt-dlp names the sheets (its "sb" formats); a sheet is fetched the first
// time a second on it is asked for, and kept for the video.
class Storyboard {
public:
    Storyboard();

    // `formats`: yt-dlp's first formats as JSON, where the storyboards are.
    void setVideo(const QString &id, const QByteArray &formats);
    void clear();
    QString video() const { return m_id; }
    int loaded() const { return int(m_images.size()) * 1000 + int(m_sheets.size()); }

    // The frame for this second; null until its sheet has come (then
    // onReady is called).
    QPixmap frameAt(double seconds);
    std::function<void()> onReady;

private:
    struct Sheet {
        QString url;
        double duration = 0;
    };
    void fetch(int sheet);
    QString m_id;
    QVector<Sheet> m_sheets;
    int m_width = 0, m_height = 0, m_rows = 0, m_columns = 0;
    double m_every = 0;   // seconds between frames
    QHash<int, QImage> m_images;
    QSet<int> m_asked;
    QNetworkAccessManager m_net;
};
