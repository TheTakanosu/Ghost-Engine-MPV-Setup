#include "thumbnails.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QNetworkReply>
#include <QStandardPaths>

namespace {

// Cost is in kilobytes of decoded pixels: about 120 card covers or a few dozen
// hero covers before the oldest are dropped, some 30 MB at most.
constexpr int kMemoryBudgetKb = 30 * 1024;

// YouTube letterboxes: a square album cover in a 16:9 video comes with black
// bars at the sides, a 4:3 one with bars above and below. Rows and columns
// that are almost all near-black at the edges are cut away, up to a third of
// the picture from each side, so the cover fills its frame.
bool darkLine(const QImage &image, int index, bool column)
{
    const int length = column ? image.height() : image.width();
    int dark = 0, samples = 0;
    for (int i = 0; i < length; i += qMax(1, length / 48)) {
        const QRgb px = column ? image.pixel(index, i) : image.pixel(i, index);
        dark += qGray(px) < 24 ? 1 : 0;
        ++samples;
    }
    return dark * 100 >= samples * 94;
}

QImage cropBars(const QImage &image)
{
    if (image.isNull())
        return image;
    const int w = image.width(), h = image.height();
    int left = 0, right = w - 1, top = 0, bottom = h - 1;
    while (left < w / 3 && darkLine(image, left, true)) ++left;
    while (right > w * 2 / 3 && darkLine(image, right, true)) --right;
    while (top < h / 3 && darkLine(image, top, false)) ++top;
    while (bottom > h * 2 / 3 && darkLine(image, bottom, false)) --bottom;
    // A thin edge is compression noise, not a bar.
    if (left < 4) left = 0;
    if (w - 1 - right < 4) right = w - 1;
    if (top < 4) top = 0;
    if (h - 1 - bottom < 4) bottom = h - 1;
    return image.copy(left, top, right - left + 1, bottom - top + 1);
}

QString key(const QString &videoId, ThumbnailCache::Size size)
{
    // A picture by its URL (search results' playlists): a file named by its hash.
    if (videoId.startsWith(QLatin1String("http")))
        return QString::fromLatin1(QCryptographicHash::hash(videoId.toUtf8(), QCryptographicHash::Sha1).toHex().left(24));
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
    image = cropBars(image);
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
    QNetworkRequest request(videoId.startsWith(QLatin1String("http"))
                                ? QUrl(videoId)
                                : QUrl(QStringLiteral("https://i.ytimg.com/vi/%1/%2").arg(videoId, file)));
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
