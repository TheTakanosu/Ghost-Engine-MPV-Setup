#pragma once

#include <QCache>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPixmap>
#include <QSet>

// YouTube covers, fetched from i.ytimg.com (no API key) and kept on disk, so
// a list is downloaded once. Only what is on screen lives in memory, in a
// bounded cache; the rest is a file read away.
class ThumbnailCache : public QObject {
    Q_OBJECT

public:
    enum class Size { Card, Hero };   // 320x180, and a 16:9 crop of 480x360

    explicit ThumbnailCache(QObject *parent = nullptr);

    // The cover if it is ready; otherwise starts fetching it and returns a
    // null pixmap. ready() fires when it arrives.
    QPixmap get(const QString &videoId, Size size);

signals:
    void ready(const QString &videoId);

private:
    QString diskPath(const QString &videoId, Size size) const;
    void fetch(const QString &videoId, Size size);

    QNetworkAccessManager m_net;
    QCache<QString, QPixmap> m_memory;
    QSet<QString> m_inFlight;
    QString m_dir;
};
