#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// The ten-band equalizer (Settings > Playback): its bands, presets and the
// setting, and the mpv audio filter made of it. Each band is ffmpeg's
// `equalizer` (a peaking filter an octave wide) named b0..b9 in one lavfi
// graph labelled @ghosteq, so a band's gain changes live with af-command
// instead of rebuilding the filter chain.
namespace equalizer {

constexpr int kBands = 10;
constexpr int kMaxGain = 12;   // dB, either way
// 31 Hz to 16 kHz, an octave apart.
const QVector<int> &frequencies();
QString bandName(int band);    // "31", "1K", "16K"

struct Preset {
    QString name;
    QVector<int> gains;
};
const QVector<Preset> &presets();
// The preset these gains are, or empty (Custom).
QString presetFor(const QVector<int> &gains);

bool enabled();
void setEnabled(bool on);
QVector<int> gains();
void setGains(const QVector<int> &gains);

// "lavfi=[equalizer@b0=f=31:t=o:w=1:g=0,...]" for `af add @ghosteq:...`.
QString filter(const QVector<int> &gains);

} // namespace equalizer
