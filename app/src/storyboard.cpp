#include "storyboard.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

namespace {

// YouTube sends the storyboard sheets as WebP whatever is asked for, and
// Qt here has no WebP reader: Windows' own codecs read it (WIC, with the
// WebP extension Windows 10 and 11 come with).
QImage decodeWithWindows(const QByteArray &data)
{
    QImage out;
    IWICImagingFactory *factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        return out;
    IWICStream *stream = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *converter = nullptr;
    UINT w = 0, h = 0;
    if (SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(reinterpret_cast<BYTE *>(const_cast<char *>(data.constData())), DWORD(data.size()))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(converter->GetSize(&w, &h)) && w > 0 && h > 0) {
        QImage image(int(w), int(h), QImage::Format_ARGB32);
        if (SUCCEEDED(converter->CopyPixels(nullptr, UINT(image.bytesPerLine()), UINT(image.sizeInBytes()), image.bits())))
            out = image;
    }
    for (IUnknown *u : {static_cast<IUnknown *>(converter), static_cast<IUnknown *>(frame),
                        static_cast<IUnknown *>(decoder), static_cast<IUnknown *>(stream), static_cast<IUnknown *>(factory)})
        if (u)
            u->Release();
    return out;
}

} // namespace

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

void Storyboard::fetch(int sheet)
{
    if (sheet < 0 || sheet >= m_sheets.size() || m_asked.contains(sheet) || m_images.contains(sheet))
        return;
    m_asked.insert(sheet);
    const QString id = m_id;
    QNetworkReply *reply = m_net.get(QNetworkRequest(QUrl(m_sheets[sheet].url)));
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, sheet, id] {
        reply->deleteLater();
        if (id != m_id)
            return;   // another video by now
        QImage got;
        if (reply->error() == QNetworkReply::NoError) {
            const QByteArray body = reply->readAll();
            if (!got.loadFromData(body))
                got = decodeWithWindows(body);
        }
        if (!got.isNull()) {
            m_images.insert(sheet, got);
            if (onReady)
                onReady();
        } else {
            m_asked.remove(sheet);   // asked again on the next hover
        }
    });
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
    // The first ask fetches every sheet, the one asked for first: the rest
    // are a few dozen kB each and the next seconds show at once.
    if (m_asked.isEmpty())
        for (int i = 0; i < m_sheets.size(); ++i)
            if (i != sheet)
                QTimer::singleShot(300 + 150 * i, &m_net, [this, i, id = m_id] { if (id == m_id) fetch(i); });
    const auto image = m_images.constFind(sheet);
    if (image == m_images.cend()) {
        fetch(sheet);
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
