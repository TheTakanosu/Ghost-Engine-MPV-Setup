#include "thumbnails.h"

#include <algorithm>

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

// YouTube Music's song pictures put a square cover in a 16:9 (or 4:3)
// frame with flat bands of one colour at the sides (or above and below):
// white, grey, a dark tint of the cover. Those bands are cut too, so the
// cover fills its square (Batın, 2026-11-11). Careful, so a cover's own
// border stays: a band is a run of lines whose samples are, 94 in 100, within
// a little of one colour (JPEG noise); both sides must have one, of the same
// colour, each at least 4% of the side; a band all round, thin on every
// side, is a frame and stays; every band cut is the same colour (a cover's
// own black border inside grey bands stays); and what is left must be
// close to square, or nothing is cut.
constexpr int kBandTolerance = 14;   // per channel, of 255 (about 5%)

QRgb lineColour(const QImage &image, int index, bool column)
{
    const int length = column ? image.height() : image.width();
    QVector<int> r, g, b;
    for (int i = 0; i < length; i += qMax(1, length / 48)) {
        const QRgb px = column ? image.pixel(index, i) : image.pixel(i, index);
        r << qRed(px);
        g << qGreen(px);
        b << qBlue(px);
    }
    std::sort(r.begin(), r.end());
    std::sort(g.begin(), g.end());
    std::sort(b.begin(), b.end());
    return qRgb(r[r.size() / 2], g[g.size() / 2], b[b.size() / 2]);
}

bool near(QRgb a, QRgb b, int tolerance = kBandTolerance)
{
    return qAbs(qRed(a) - qRed(b)) <= tolerance && qAbs(qGreen(a) - qGreen(b)) <= tolerance &&
           qAbs(qBlue(a) - qBlue(b)) <= tolerance;
}

bool flatLine(const QImage &image, int index, bool column, QRgb colour)
{
    const int length = column ? image.height() : image.width();
    int same = 0, samples = 0;
    for (int i = 0; i < length; i += qMax(1, length / 48)) {
        same += near(column ? image.pixel(index, i) : image.pixel(i, index), colour) ? 1 : 0;
        ++samples;
    }
    return same * 100 >= samples * 94;
}

// How many lines from `start` inward are one flat colour; that colour.
int bandFrom(const QImage &image, int start, int step, int limit, bool column, QRgb *colour)
{
    *colour = lineColour(image, start, column);
    int n = 0;
    for (int i = start; n < limit && flatLine(image, i, column, *colour); i += step)
        ++n;
    return n;
}

QImage cropFlatBands(const QImage &image)
{
    QRect box = image.rect();
    bool have = false;
    QRgb chain = 0;
    for (int pass = 0; pass < 2; ++pass) {
        const QImage part = image.copy(box);
        const int w = part.width(), h = part.height();
        QRgb lc, rc, tc, bc;
        const int left = bandFrom(part, 0, 1, w / 3, true, &lc);
        const int right = bandFrom(part, w - 1, -1, w / 3, true, &rc);
        const int top = bandFrom(part, 0, 1, h / 3, false, &tc);
        const int bottom = bandFrom(part, h - 1, -1, h / 3, false, &bc);
        const bool sides = left >= w * 4 / 100 && right >= w * 4 / 100 && near(lc, rc, 2 * kBandTolerance);
        const bool ends = top >= h * 4 / 100 && bottom >= h * 4 / 100 && near(tc, bc, 2 * kBandTolerance);
        if (sides && ends && qMin(left, right) * 10 < w && qMin(top, bottom) * 10 < h && near(lc, tc, 2 * kBandTolerance))
            return image;   // a thin frame all round: the cover's own
        if (!sides && !ends)
            break;
        const QRgb colour = sides ? lc : tc;
        if (have && !near(colour, chain, 2 * kBandTolerance))
            break;   // another colour inside: the cover's own border
        if (have && sides && ends && !near(lc, tc, 2 * kBandTolerance))
            break;
        chain = colour;
        have = true;
        box = QRect(box.left() + (sides ? left : 0), box.top() + (ends ? top : 0),
                    w - (sides ? left + right : 0), h - (ends ? top + bottom : 0));
    }
    if (!have || box.width() < 16 || box.height() < 16)
        return image;
    const double aspect = double(box.width()) / box.height();
    return aspect >= 0.85 && aspect <= 1.18 ? image.copy(box) : image;
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
    image = cropFlatBands(cropBars(image.convertToFormat(QImage::Format_RGB32)));
    // Kept no larger than it is ever drawn (a picture by URL came 1000 px
    // wide): scaling it down once here instead of at every painted frame.
    const int cap = size == Size::Hero ? 640 : 400;
    if (image.width() > cap)
        image = image.scaledToWidth(cap, Qt::SmoothTransformation);
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
