#include "thumbnails.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QNetworkReply>
#include <QStandardPaths>

namespace {

// Cost is in kilobytes of decoded pixels: about 120 card covers or a few dozen
// hero covers before the oldest are dropped, some 30 MB at most.
constexpr int kMemoryBudgetKb = 30 * 1024;

QString key(const QString &videoId, ThumbnailCache::Size size)
{
    return videoId + (size == ThumbnailCache::Size::Hero ? QStringLiteral("-hq") : QStringLiteral("-mq"));
}

} // namespace

ThumbnailCache::ThumbnailCache(QObject *parent) : QObject(parent)
{
    m_memory.setMaxCost(kMemoryBudgetKb);
    m_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/covers");
    QDir().mkpath(m_dir);
}

QString ThumbnailCache::diskPath(const QString &videoId, Size size) const
{
    return m_dir + '/' + key(videoId, size) + QStringLiteral(".jpg");
}

QPixmap ThumbnailCache::get(const QString &videoId, Size size)
{
    if (videoId.isEmpty())
        return {};
    const QString k = key(videoId, size);
    if (QPixmap *hit = m_memory.object(k))
        return *hit;

    QImage image(diskPath(videoId, size));
    if (image.isNull()) {
        fetch(videoId, size);
        return {};
    }
    if (size == Size::Hero && image.height() * 16 != image.width() * 9) {
        // hqdefault is 4:3 with black bars above and below a 16:9 picture.
        const int h = image.width() * 9 / 16;
        image = image.copy(0, (image.height() - h) / 2, image.width(), h);
    }
    auto *pixmap = new QPixmap(QPixmap::fromImage(image));
    const int costKb = qMax(1, pixmap->width() * pixmap->height() * 4 / 1024);
    m_memory.insert(k, pixmap, costKb);
    return *pixmap;
}

void ThumbnailCache::fetch(const QString &videoId, Size size)
{
    const QString k = key(videoId, size);
    if (m_inFlight.contains(k))
        return;
    m_inFlight.insert(k);

    const QString file = size == Size::Hero ? QStringLiteral("hqdefault.jpg") : QStringLiteral("mqdefault.jpg");
    QNetworkRequest request(QUrl(QStringLiteral("https://i.ytimg.com/vi/%1/%2").arg(videoId, file)));
    request.setTransferTimeout(10000);
    QNetworkReply *reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, videoId, size, k] {
        reply->deleteLater();
        m_inFlight.remove(k);
        if (reply->error() != QNetworkReply::NoError)
            return;
        QFile out(diskPath(videoId, size));
        if (out.open(QIODevice::WriteOnly)) {
            out.write(reply->readAll());
            out.close();
            emit ready(videoId);
        }
    });
}
