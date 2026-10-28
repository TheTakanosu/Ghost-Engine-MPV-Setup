#pragma once

#include <QColor>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <functional>

// A song's words: timed lines when the source has them, else plain text.
struct Lyrics {
    struct Line {
        double at = 0;   // seconds into the song
        QString text;
    };
    QVector<Line> lines;   // timed, in order; empty if only plain text
    QString plain;
    bool found = false;
};

// Finds lyrics on LRCLIB (lrclib.net, free, no key) and keeps each answer
// on disk, a miss included, so a song is asked about once.
class LyricsSource : public QObject {
    Q_OBJECT

public:
    explicit LyricsSource(QObject *parent = nullptr);

    // `duration` in seconds, 0 if unknown. The answer arrives as ready().
    void fetch(const QString &artist, const QString &track, double duration);
    static QString keyFor(const QString &artist, const QString &track);

signals:
    void ready(const QString &key, const Lyrics &lyrics);

private:
    void ask(const QString &key, const QString &query, double duration, int attempt, bool broad);
    // `key` says whose song it is: a result by another artist is never
    // taken, however well its length fits.
    Lyrics pick(const QByteArray &json, double duration, const QString &key) const;
    QString cacheFile(const QString &key) const;
    void remember(const QString &key, const Lyrics &lyrics, const QByteArray &raw);

    QNetworkAccessManager m_net;
    QString m_dir;
};

// The lyrics page: the words in large type over the cover's colour, the
// line being sung bright and kept in view, a click on a line seeks there.
class LyricsView : public QWidget {
public:
    explicit LyricsView(QWidget *parent = nullptr);

    void setLyrics(const Lyrics &lyrics, const QString &title);
    void setSearching(const QString &title);
    void setPosition(double seconds);
    void setTint(const QColor &tint);
    std::function<void(double seconds)> onSeek;
    // Shown when nothing is found: a search for the song on Genius, in the
    // browser (Genius lets no app take its lyrics).
    void setSearchText(const QString &text);

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    int currentLine() const;
    QVector<QRectF> layoutLines(qreal width) const;
    void followCurrent();
    QRectF textArea() const;
    int lineAt(const QPointF &pos) const;
    QStringList lineTexts() const;
    QString selectedText() const;   // the selected lines, or all of them
    void copyLyrics();
    void saveLyrics();
    void resizeEvent(QResizeEvent *) override;

    Lyrics m_lyrics;
    QString m_title;
    QString m_state;    // "searching", "missing", or empty
    QColor m_tint;
    double m_position = 0;
    qreal m_scroll = 0, m_targetScroll = 0;
    qint64 m_userScrolledAt = 0;   // the wheel takes over for a few seconds
    QTimer m_glide;                // eases the scroll towards its target
    class QPushButton *m_genius;
    class QPushButton *m_copy;
    class QPushButton *m_save;
    int m_selFrom = -1, m_selTo = -1;   // selected lines, either order
    int m_pressLine = -1;
    QPointF m_pressAt;
    bool m_selecting = false;
    QString m_searchText;
};
