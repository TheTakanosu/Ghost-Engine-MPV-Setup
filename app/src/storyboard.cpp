#include "storyboard.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

Storyboard::Storyboard() = default;

void Storyboard::clear()
{
    m_id.clear();
    m_sheets.clear();
    m_images.clear();
    m_asked.clear();
    m_width = m_height = m_rows = m_columns = 0;
    m_every = 0;
}

void Storyboard::setVideo(const QString &id, const QByteArray &formats)
{
    if (id == m_id && !m_sheets.isEmpty())
        return;
    clear();
    m_id = id;
    // The sharpest storyboard up to 200 px wide (sb1 on most videos: 160x90
    // in sheets of 5x5); sb0 is larger but cut into many more sheets.
    QJsonObject best;
    for (const QJsonValue &v : QJsonDocument::fromJson(formats).array()) {
        const QJsonObject f = v.toObject();
        if (!f.value("format_id").toString().startsWith(QLatin1String("sb")))
            continue;
        const int w = f.value("width").toInt();
        if (w > 200 || f.value("fragments").toArray().isEmpty())
            continue;
        if (best.isEmpty() || w > best.value("width").toInt())
            best = f;
    }
    if (best.isEmpty())
        return;
    m_width = best.value("width").toInt();
    m_height = best.value("height").toInt();
    m_rows = best.value("rows").toInt();
    m_columns = best.value("columns").toInt();
    const double fps = best.value("fps").toDouble();
    m_every = fps > 0 ? 1.0 / fps : 0;
    for (const QJsonValue &v : best.value("fragments").toArray()) {
        const QJsonObject s = v.toObject();
        m_sheets.append({s.value("url").toString(), s.value("duration").toDouble()});
    }
    if (m_width <= 0 || m_height <= 0 || m_rows <= 0 || m_columns <= 0 || m_every <= 0)
        clear();
}

QPixmap Storyboard::frameAt(double seconds)
{
    if (m_sheets.isEmpty() || seconds < 0)
        return {};
    // Which sheet: by their lengths, the last one usually shorter.
    int sheet = 0;
    double start = 0;
    while (sheet < m_sheets.size() - 1 && seconds >= start + m_sheets[sheet].duration) {
        start += m_sheets[sheet].duration;
        ++sheet;
    }
    const int perSheet = m_rows * m_columns;
    const int tile = qBound(0, int((seconds - start) / m_every), perSheet - 1);
    const auto image = m_images.constFind(sheet);
    if (image == m_images.cend()) {
        if (!m_asked.contains(sheet)) {
            m_asked.insert(sheet);
            const QString id = m_id;
            QNetworkReply *reply = m_net.get(QNetworkRequest(QUrl(m_sheets[sheet].url)));
            QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, sheet, id] {
                reply->deleteLater();
                if (id != m_id)
                    return;   // another video by now
                QImage got;
                if (reply->error() == QNetworkReply::NoError && got.loadFromData(reply->readAll())) {
                    m_images.insert(sheet, got);
                    if (onReady)
                        onReady();
                } else {
                    m_asked.remove(sheet);   // asked again on the next hover
                }
            });
        }
        return {};
    }
    // The cells: the stated frame size, unless the sheet is a few pixels
    // off it; the last sheet can have fewer rows than the others.
    const int cw = image->width() >= m_width * m_columns ? m_width : image->width() / m_columns;
    const int rowsHere = qMax(1, qRound(double(image->height()) / m_height));
    const int ch = image->height() / rowsHere;
    if (tile / m_columns >= rowsHere)
        return {};
    const QRect cell((tile % m_columns) * cw, (tile / m_columns) * ch, cw, ch);
    return QPixmap::fromImage(image->copy(cell));
}
