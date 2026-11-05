#pragma once

#include <QColor>
#include <QString>

#include <functional>

class QApplication;
class QObject;
class QWidget;

// One place for the look. Every colour the app draws or styles is one of the
// roles below; a theme is only a set of values for them. The style sheet and
// the widgets' own sheets name the roles (@card, @text...), so changing the
// theme restyles everything without a restart.
namespace theme {

struct Palette {
    QColor base;          // behind everything: the window's edges, the top strip, the player bar
    QColor window;        // the library, the page and the side panel
    QColor card;          // a card on the page, a button, a field, a menu
    QColor cardAlt;       // a card inside a card (the panel's lyrics, the video's description)
    QColor cardAltHover;
    QColor raised;        // a card under the mouse, a row being shown
    QColor raisedHover;
    QColor pressed;
    QColor cardSelected;
    QColor popup;         // the tray menu and the Updates window
    QColor lineSoft;      // hairlines between columns
    QColor line;          // a card's or a field's border
    QColor lineStrong;
    QColor tick;          // marks on a scale, a dashed outline
    QColor track;         // a slider's groove, a switch that is off
    QColor trackHover;
    QColor scroll;
    QColor scrollHover;
    QColor select;        // a menu item under the mouse
    QColor toast;
    QColor accent;
    QColor accentHover;
    QColor accentText;    // accent-coloured text and numbers
    QColor onAccent;      // text on an accent background
    QColor strong;        // the brightest text: a hovered icon, a name
    QColor textBright;
    QColor text;
    QColor textSoft;
    QColor textBody;      // longer text: an artist's about, a hint
    QColor dim;           // an icon at rest
    QColor muted;         // secondary text
    QColor faint;         // a source line, the least important text
    QColor danger;        // the close button under the mouse
    bool light = false;   // dark text on light ground: covers and icons pick their contrast from it
};

// The built-in themes, as Settings lists them. System follows Windows: Light
// when Windows is light, Default when it is dark.
enum class Kind { Default, Black, Light, System };
Kind kind();
QString kindName(Kind kind);
void setKind(QApplication &app, Kind kind);
// The user's own accent over the theme's (Settings > Appearance), or an
// invalid colour for the theme's own. The other accent roles follow it.
QColor customAccent();
void setAccent(QApplication &app, const QColor &color);

const Palette &c();

void apply(QApplication &app);

// A widget's own style sheet with role names in it, kept so it is restyled
// when the theme changes. css() fills in the names once, for text that is
// set again anyway.
void style(QWidget *widget, const QString &sheet);
QString css(const QString &sheet);

// For colours handed to a widget once (an icon button's, say): run again
// whenever the theme changes, for as long as the owner lives.
void whenChanged(QObject *owner, std::function<void()> refresh);

// Ctrl + / Ctrl - : every font and row grows together. 1.0 is normal.
qreal zoom();
// The top strip's controls follow the zoom only up to 120%.
qreal chromeZoom();
void setZoom(QApplication &app, qreal factor);

// The roles the app used before there were themes, kept for the many callers.
inline QColor window() { return c().window; }
inline QColor card() { return c().card; }
inline QColor cardHover() { return c().raised; }
inline QColor cardSelected() { return c().cardSelected; }
inline QColor accent() { return c().accent; }
inline QColor accentText() { return c().accentText; }
inline QColor text() { return c().text; }
inline QColor muted() { return c().muted; }

// A muted tint picked from the name, so a cover-less list keeps its colour.
QColor placeholderFor(const QString &name);

} // namespace theme
