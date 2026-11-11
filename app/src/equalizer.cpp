#include "equalizer.h"

#include <QSettings>
#include <QVariant>

namespace equalizer {

const QVector<int> &frequencies()
{
    static const QVector<int> hz{31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    return hz;
}

QString bandName(int band)
{
    const int hz = frequencies().value(band);
    return hz >= 1000 ? QStringLiteral("%1K").arg(hz / 1000) : QString::number(hz);
}

const QVector<Preset> &presets()
{
    static const QVector<Preset> all{
        {QStringLiteral("Flat"),         {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {QStringLiteral("Bass boost"),   {6, 5, 4, 2, 0, 0, 0, 0, 0, 0}},
        {QStringLiteral("Treble boost"), {0, 0, 0, 0, 0, 1, 2, 4, 5, 6}},
        {QStringLiteral("Vocal"),        {-2, -2, -1, 1, 3, 4, 3, 1, 0, -1}},
        {QStringLiteral("Rock"),         {4, 3, 2, 0, -1, -1, 1, 2, 3, 4}},
        {QStringLiteral("Electronic"),   {5, 4, 1, 0, -2, 1, 0, 1, 4, 5}},
    };
    return all;
}

QString presetFor(const QVector<int> &gains)
{
    for (const Preset &p : presets())
        if (p.gains == gains)
            return p.name;
    return {};
}

bool enabled()
{
    return QSettings().value("eq/on", false).toBool();
}

void setEnabled(bool on)
{
    QSettings().setValue("eq/on", on);
}

QVector<int> gains()
{
    const QStringList kept = QSettings().value("eq/gains").toString().split(',', Qt::SkipEmptyParts);
    QVector<int> out(kBands, 0);
    for (int i = 0; i < kBands && i < kept.size(); ++i)
        out[i] = qBound(-kMaxGain, kept[i].toInt(), kMaxGain);
    return out;
}

void setGains(const QVector<int> &gains)
{
    QStringList text;
    for (int g : gains)
        text << QString::number(g);
    QSettings().setValue("eq/gains", text.join(','));
}

QString filter(const QVector<int> &gains)
{
    QStringList bands;
    for (int i = 0; i < kBands; ++i)
        bands << QStringLiteral("equalizer@b%1=f=%2:t=o:w=1:g=%3").arg(i).arg(frequencies()[i]).arg(gains.value(i));
    // Boosts would clip at full scale: the whole is lowered by the largest
    // boost, so a +6 dB bass stays clean (as Spotify's does).
    int most = 0;
    for (int g : gains)
        most = qMax(most, g);
    bands << QStringLiteral("volume@pre=volume=%1dB").arg(-most);
    return QStringLiteral("lavfi=[%1]").arg(bands.join(','));
}

} // namespace equalizer
