#include "theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QHash>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QSettings>
#include <cmath>
#include <QStyleHints>
#include <QWidget>
#include <QEvent>
#include <QComboBox>
#include <QMenu>
#include <QStyledItemDelegate>
#include <QFont>
#include <QPalette>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QStyleFactory>

namespace theme {

namespace {

qreal g_zoom = 1.0;

// Ghost Engine's own look: soft dark greys with a violet accent. The values
// are the ones the app had before themes, so Default looks as it always did.
Palette defaultPalette()
{
    Palette p;
    p.base = {12, 11, 15};         p.window = {22, 21, 27};        p.card = {33, 32, 41};
    p.cardAlt = {36, 35, 45};      p.cardAltHover = {42, 41, 52};
    p.raised = {40, 39, 50};       p.raisedHover = {46, 44, 58};   p.pressed = {56, 54, 70};
    p.cardSelected = {46, 40, 62}; p.popup = {30, 29, 37};
    p.lineSoft = {44, 42, 54};     p.line = {52, 50, 64};          p.lineStrong = {58, 56, 70};
    p.tick = {90, 88, 104};        p.track = {60, 58, 72};         p.trackHover = {76, 74, 90};
    p.scroll = {72, 70, 86};       p.scrollHover = {104, 100, 122};
    p.select = {60, 48, 84};       p.toast = {44, 38, 60};
    p.accent = {176, 127, 224};    p.accentHover = {196, 152, 240};
    p.accentText = {207, 179, 240}; p.onAccent = {24, 18, 34};
    p.strong = {255, 255, 255};    p.textBright = {240, 240, 245}; p.text = {236, 236, 241};
    p.textSoft = {220, 218, 228};  p.textBody = {200, 198, 210};
    p.dim = {170, 168, 182};       p.muted = {154, 152, 166};      p.faint = {120, 118, 132};
    p.danger = {196, 43, 28};
    return p;
}

// Black: the same layout in neutral greys, the accent a near-white. Nothing
// is tinted, for those who want the music and not the colour.
Palette blackPalette()
{
    Palette p;
    p.base = {0, 0, 0};            p.window = {14, 14, 14};        p.card = {26, 26, 26};
    p.cardAlt = {30, 30, 30};      p.cardAltHover = {38, 38, 38};
    p.raised = {36, 36, 36};       p.raisedHover = {44, 44, 44};   p.pressed = {56, 56, 56};
    p.cardSelected = {44, 44, 44}; p.popup = {22, 22, 22};
    p.lineSoft = {38, 38, 38};     p.line = {48, 48, 48};          p.lineStrong = {58, 58, 58};
    p.tick = {88, 88, 88};         p.track = {58, 58, 58};         p.trackHover = {76, 76, 76};
    p.scroll = {66, 66, 66};       p.scrollHover = {100, 100, 100};
    p.select = {52, 52, 52};       p.toast = {36, 36, 36};
    p.accent = {232, 232, 232};    p.accentHover = {255, 255, 255};
    p.accentText = {245, 245, 245}; p.onAccent = {10, 10, 10};
    p.strong = {255, 255, 255};    p.textBright = {242, 242, 242}; p.text = {236, 236, 236};
    p.textSoft = {218, 218, 218};  p.textBody = {196, 196, 196};
    p.dim = {164, 164, 164};       p.muted = {148, 148, 148};      p.faint = {112, 112, 112};
    p.danger = {196, 43, 28};
    return p;
}

// Light: white cards on a pale grey, dark text, the violet deepened so it
// still reads on white.
Palette lightPalette()
{
    Palette p;
    p.base = {232, 231, 237};      p.window = {246, 245, 249};     p.card = {255, 255, 255};
    p.cardAlt = {240, 239, 245};   p.cardAltHover = {232, 230, 239};
    p.raised = {236, 234, 242};    p.raisedHover = {226, 224, 234}; p.pressed = {214, 211, 224};
    p.cardSelected = {232, 224, 248}; p.popup = {252, 252, 254};
    p.lineSoft = {226, 224, 232};  p.line = {214, 212, 222};       p.lineStrong = {200, 198, 210};
    p.tick = {170, 168, 182};      p.track = {208, 206, 216};      p.trackHover = {190, 188, 200};
    p.scroll = {196, 194, 206};    p.scrollHover = {160, 158, 172};
    p.select = {230, 220, 248};    p.toast = {246, 240, 254};
    p.accent = {128, 74, 196};     p.accentHover = {112, 60, 178};
    p.accentText = {110, 58, 176}; p.onAccent = {255, 255, 255};
    p.strong = {0, 0, 0};          p.textBright = {20, 19, 26};    p.text = {28, 27, 34};
    p.textSoft = {48, 47, 58};     p.textBody = {70, 68, 82};
    p.dim = {96, 94, 108};         p.muted = {104, 102, 116};      p.faint = {136, 134, 148};
    p.danger = {196, 43, 28};
    p.light = true;
    return p;
}

Kind g_kind = Kind::Default;
Palette g_palette = defaultPalette();
QColor g_accent;   // invalid: the theme's own

// One chosen colour made into the four accent roles, for the theme it sits
// on: a hover a shade towards the light (dark themes) or the dark (Light),
// text that stays readable on the theme's panels, and black or white on it.
void useAccent(Palette &p, const QColor &color)
{
    const bool dark = p.window.lightness() < 128;
    p.accent = color;
    p.accentHover = dark ? color.lighter(115) : color.darker(115);
    if (dark) {
        // Towards white until it reads on the dark panels.
        QColor t = color;
        for (int i = 0; i < 6 && t.lightness() < 170; ++i)
            t = QColor::fromRgbF(t.redF() + (1 - t.redF()) * 0.2, t.greenF() + (1 - t.greenF()) * 0.2,
                                 t.blueF() + (1 - t.blueF()) * 0.2);
        p.accentText = t;
    } else {
        QColor t = color;
        for (int i = 0; i < 6 && t.lightness() > 110; ++i)
            t = t.darker(118);
        p.accentText = t;
    }
    // Relative luminance: light accents carry black text, dark ones white.
    const auto lin = [](qreal v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    const qreal lum = 0.2126 * lin(color.redF()) + 0.7152 * lin(color.greenF()) + 0.0722 * lin(color.blueF());
    p.onAccent = lum > 0.35 ? QColor(16, 14, 22) : QColor(255, 255, 255);
}

bool windowsIsLight()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light;
}

void choosePalette()
{
    switch (g_kind) {
    case Kind::Black: g_palette = blackPalette(); break;
    case Kind::Light: g_palette = lightPalette(); break;
    case Kind::System: g_palette = windowsIsLight() ? lightPalette() : defaultPalette(); break;
    case Kind::Default: g_palette = defaultPalette(); break;
    }
    if (g_accent.isValid())
        useAccent(g_palette, g_accent);
}

const QHash<QString, QColor Palette::*> &roles()
{
    static const QHash<QString, QColor Palette::*> table = {
        {"base", &Palette::base}, {"window", &Palette::window}, {"card", &Palette::card},
        {"cardAlt", &Palette::cardAlt}, {"cardAltHover", &Palette::cardAltHover},
        {"raised", &Palette::raised}, {"raisedHover", &Palette::raisedHover}, {"pressed", &Palette::pressed},
        {"cardSelected", &Palette::cardSelected}, {"popup", &Palette::popup},
        {"lineSoft", &Palette::lineSoft}, {"line", &Palette::line}, {"lineStrong", &Palette::lineStrong},
        {"tick", &Palette::tick}, {"track", &Palette::track}, {"trackHover", &Palette::trackHover},
        {"scroll", &Palette::scroll}, {"scrollHover", &Palette::scrollHover},
        {"select", &Palette::select}, {"toast", &Palette::toast},
        {"accent", &Palette::accent}, {"accentHover", &Palette::accentHover},
        {"accentText", &Palette::accentText}, {"onAccent", &Palette::onAccent},
        {"strong", &Palette::strong}, {"textBright", &Palette::textBright}, {"text", &Palette::text},
        {"textSoft", &Palette::textSoft}, {"textBody", &Palette::textBody},
        {"dim", &Palette::dim}, {"muted", &Palette::muted}, {"faint", &Palette::faint},
        {"danger", &Palette::danger},
    };
    return table;
}

// "@card" -> "rgb(33,32,41)". The whole run of letters is the name, so
// @textBright is never read as @text; a name that is not a role is left as
// it was written.
QString fill(const QString &sheet)
{
    static const QRegularExpression name(QStringLiteral(R"(@([A-Za-z]+))"));
    QString out;
    qsizetype last = 0;
    for (auto it = name.globalMatch(sheet); it.hasNext();) {
        const auto m = it.next();
        const auto role = roles().constFind(m.captured(1));
        if (role == roles().constEnd())
            continue;
        const QColor &c = g_palette.*(*role);
        out += sheet.mid(last, m.capturedStart() - last) +
               QStringLiteral("rgb(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue());
        last = m.capturedEnd();
    }
    return out + sheet.mid(last);
}

const char *const kSheetProperty = "ghostThemeSheet";

QList<QPair<QPointer<QObject>, std::function<void()>>> &refreshers()
{
    static QList<QPair<QPointer<QObject>, std::function<void()>>> list;
    return list;
}

} // namespace

qreal zoom() { return g_zoom; }
qreal chromeZoom() { return qMin(g_zoom, 1.2); }

const Palette &c() { return g_palette; }

Kind kind() { return g_kind; }

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Black: return QStringLiteral("black");
    case Kind::Light: return QStringLiteral("light");
    case Kind::System: return QStringLiteral("system");
    case Kind::Default: break;
    }
    return QStringLiteral("default");
}

QString css(const QString &sheet) { return fill(sheet); }

void style(QWidget *widget, const QString &sheet)
{
    widget->setProperty(kSheetProperty, sheet);
    widget->setStyleSheet(fill(sheet));
}

void whenChanged(QObject *owner, std::function<void()> refresh)
{
    refreshers().append({owner, std::move(refresh)});
}

QColor customAccent()
{
    return g_accent;
}

void setAccent(QApplication &app, const QColor &color)
{
    g_accent = color;
    if (color.isValid())
        QSettings().setValue("look/accent", color.name());
    else
        QSettings().remove("look/accent");
    apply(app);
}

void setKind(QApplication &app, Kind kind)
{
    g_kind = kind;
    QSettings().setValue("look/theme", kindName(kind));
    apply(app);
}

QColor placeholderFor(const QString &name)
{
    static const QColor tints[] = {
        {74, 58, 104}, {46, 78, 92}, {92, 60, 70}, {58, 84, 66}, {88, 72, 48}, {52, 62, 102},
    };
    return tints[qHash(name) % (sizeof tints / sizeof tints[0])];
}

namespace {

// A click on a slider's track jumps there, as a seek bar should, instead of
// stepping a page towards the click.
class ClickToJumpStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                  QStyleHintReturn *returnData) const override
    {
        if (hint == SH_Slider_AbsoluteSetButtons)
            return Qt::LeftButton;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

};

} // namespace

// A menu is its own window, and the rounded box QSS draws sat on its square
// corners. A see-through window shows only the box; Windows' square shadow
// would outline the corners again, so it goes too.
class RoundMenus : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Polish)
            return false;
        if (auto *menu = qobject_cast<QMenu *>(watched)) {
            menu->setWindowFlag(Qt::FramelessWindowHint);
            menu->setWindowFlag(Qt::NoDropShadowWindowHint);
            menu->setAttribute(Qt::WA_TranslucentBackground);
        } else if (auto *combo = qobject_cast<QComboBox *>(watched)) {
            // Qt's own combo delegate ignores the item rules above.
            if (!qobject_cast<QStyledItemDelegate *>(combo->itemDelegate()))
                combo->setItemDelegate(new QStyledItemDelegate(combo));
        } else if (watched->inherits("QComboBoxPrivateContainer")) {
            auto *box = static_cast<QWidget *>(watched);
            box->setWindowFlag(Qt::FramelessWindowHint);
            box->setWindowFlag(Qt::NoDropShadowWindowHint);
            box->setAttribute(Qt::WA_TranslucentBackground);
        }
        return false;
    }
};

void apply(QApplication &app)
{
    static bool started = false;
    if (!started) {
        started = true;
        const QString saved = QSettings().value("look/theme").toString();
        for (Kind k : {Kind::Black, Kind::Light, Kind::System})
            if (saved == kindName(k))
                g_kind = k;
        const QColor accent(QSettings().value("look/accent").toString());
        if (accent.isValid())
            g_accent = accent;
        // System follows Windows while the app is open, too.
        QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [&app] {
            if (g_kind == Kind::System)
                apply(app);
        });
    }
    app.setStyle(new ClickToJumpStyle(QStyleFactory::create("Fusion")));
    app.installEventFilter(new RoundMenus(&app));
    QFont font("Segoe UI");
    font.setPointSizeF(10 * g_zoom);
    app.setFont(font);

    choosePalette();
    QPalette p;
    p.setColor(QPalette::Window, window());
    p.setColor(QPalette::WindowText, text());
    p.setColor(QPalette::Base, window());
    p.setColor(QPalette::AlternateBase, card());
    p.setColor(QPalette::Text, text());
    p.setColor(QPalette::Button, card());
    p.setColor(QPalette::ButtonText, text());
    p.setColor(QPalette::Highlight, accent());
    p.setColor(QPalette::HighlightedText, c().onAccent);
    p.setColor(QPalette::PlaceholderText, muted());
    p.setColor(QPalette::ToolTipBase, card());
    p.setColor(QPalette::ToolTipText, text());
    app.setPalette(p);

    QString css = fill(QStringLiteral(R"(
        QWidget#central { background: @base; }
        QWidget#topBar { background: @base; }
        QWidget#libraryPanel, QWidget#panel { background: @window; border-radius: 12px; }
        QWidget#home, QStackedWidget, QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
        QPushButton#bigPlay {
            font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; background: @accent; border: none;
            color: @onAccent; border-radius: 27px; font-size: 15pt;
            min-width: 54px; max-width: 54px; min-height: 54px; max-height: 54px; padding: 0;
        }
        QPushButton#bigPlay:hover { background: @accentHover; }
        QPushButton#bigGlyph {
            font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; background: transparent; border: none;
            color: @dim; font-size: 17pt; padding: 4px;
        }
        QPushButton#bigGlyph:hover { color: @strong; }
        QWidget#columns { border-bottom: 1px solid @lineSoft; }
        QLabel#columnLabel { color: @muted; font-size: 9pt; }
        QPushButton#link { background: transparent; border: none; color: @accent; padding: 0 4px; }
        QPushButton#link:hover { color: @accentHover; text-decoration: underline; }
        QLabel#npVolume { font-weight: 800; font-size: 9.5pt; color: @text; }
        QWidget#downloadRow { background: @raised; border-radius: 8px; }
        QLabel#videoTitle { font-size: 15pt; font-weight: 700; color: @textBright; }
        QPushButton#videoAction { background: @raised; border: none; border-radius: 16px; padding: 0 14px;
                                  min-height: 32px; max-height: 32px; color: @text; font-family: "Segoe UI", "Segoe Fluent Icons"; }
        QPushButton#videoAction:hover { background: @pressed; }
        QPushButton#videoControl { font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; background: transparent; border: none;
                                   color: rgb(236,236,241); font-size: 9.5pt; min-width: 28px; max-width: 28px; min-height: 28px; max-height: 28px; border-radius: 14px; }
        QPushButton#videoControl:hover, QPushButton#videoTextControl:hover { background: rgba(255,255,255,26); color: white; }
        QPushButton#videoTextControl { background: transparent; border: none; color: rgb(236,236,241); font-weight: 600;
                                       min-height: 28px; max-height: 28px; padding: 0 7px; border-radius: 14px; }
        QPushButton#miniControl { font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; background: transparent; border: none;
                                  color: white; font-size: 8pt; min-width: 24px; max-width: 24px; min-height: 24px; max-height: 24px; border-radius: 12px; }
        QPushButton#miniControl:hover { background: rgba(255,255,255,30); }
        QScrollArea#videoInfo { background: transparent; border: none; }
        /* On the picture: light whatever the theme. */
        QLabel#videoTime { color: rgb(236,236,241); font-weight: 600; }
        QWidget#videoScreen { background: transparent; }
        QLabel#videoChannel { color: @dim; font-weight: 600; }
        QWidget#videoDescCard { background: @cardAlt; border-radius: 12px; }
        QWidget#videoDescCard:hover { background: @cardAltHover; }
        QLabel#videoMeta { color: @textBright; font-weight: 700; }
        QTextBrowser#videoDescription { background: transparent; border: none; color: @textSoft; }
        QPushButton#videoMore { background: transparent; border: none; padding: 0; color: @textBright; font-weight: 700; }
        QPushButton#videoMore:hover { text-decoration: underline; }
        QWidget#videoAway { background: @base; border-radius: 12px; }
        QLabel#settingName { font-size: 10.5pt; font-weight: 600; color: @text; }
        QComboBox { background: @raised; border: 1px solid @lineStrong; border-radius: 8px; padding: 6px 12px; color: @text;
                    combobox-popup: 0; }
        QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 26px; border: none; background: transparent; }
        QComboBox::down-arrow { image: url(:/assets/chevron-down.png); width: 12px; height: 12px; }
        QComboBox:hover { border-color: @accent; }
        QComboBox QAbstractItemView { background: @card; border: 1px solid @line; border-radius: 8px;
                                      outline: none; padding: 4px; }
        QComboBox QAbstractItemView::item { padding: 6px 14px; border-radius: 6px; color: @text; min-height: 20px; }
        QComboBox QAbstractItemView::item:selected, QComboBox QAbstractItemView::item:hover { background: @select; }
        QComboBoxPrivateContainer { background: transparent; border: none; }
        QLabel#toast { background: @toast; color: @text; border: 1px solid @accent; border-radius: 16px; font-weight: 600; }
        QPushButton#columnButton, QPushButton#clockHeader {
            background: transparent; border: none; padding: 0; color: @muted; font-size: 9pt; text-align: left;
        }
        QPushButton#columnButton:hover { color: @text; }
        QPushButton#columnButton[active="true"] { color: @text; font-weight: 600; }
        QLineEdit#nameEdit { background: rgba(0,0,0,90); border: 1px solid @accent; border-radius: 8px;
                             padding: 0 6px; font-weight: 800; color: white; }
        QLineEdit#songSearch { border-radius: 16px; padding: 0 14px; min-height: 32px; max-height: 32px; }
        QLabel#heroInfo { color: @textSoft; font-size: 10pt; }
        QLabel#panelTitle { font-size: 11.5pt; font-weight: 700; color: @textSoft; }
        QListWidget#library, QListWidget#songs { background: transparent; border: none; outline: none; }
        QPushButton#glyph, QPushButton#pill, QPushButton#chrome, QPushButton#chromeClose {
            font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; background: transparent; border: none;
            color: @dim; font-size: 11pt; padding: 6px; border-radius: 8px;
        }
        QPushButton#glyph:hover { color: @strong; background: @raised; }
        QPushButton#pill {
            background: @card; color: @text; border-radius: 17px;
            min-width: 34px; max-width: 34px; min-height: 34px; max-height: 34px; padding: 0; font-size: 12pt;
        }
        QPushButton#pill:hover { background: @raisedHover; }
        QPushButton#pill:disabled { color: @muted; background: transparent; }
        QPushButton#chrome, QPushButton#chromeClose {
            border-radius: 0; min-width: 46px; max-width: 46px; min-height: 44px; max-height: 44px; padding: 0; font-size: 9pt;
        }
        QPushButton#chrome:hover { background: @raised; color: @strong; }
        QPushButton#chromeClose:hover { background: @danger; color: white; }
        QLineEdit#topSearch { background: @card; border-radius: 17px; padding: 0 16px 0 6px; min-height: 34px; max-height: 34px; }
        QPushButton#chip { background: @raised; border: none; border-radius: 14px; padding: 0 12px;
            min-height: 28px; max-height: 28px; font-size: 9pt; }
        QPushButton#chip:hover { background: @pressed; }
        QPushButton#chip:checked { background: @text; color: @window; }
        QPushButton#primary {
            background: @accent; border: none; color: @onAccent; font-weight: 700;
            border-radius: 20px; padding: 0 28px; min-height: 40px; max-height: 40px;
        }
        QPushButton#primary:hover { background: @accentHover; }
        QPushButton#primary:disabled { background: @card; color: @muted; }
        QLabel#kicker { color: @muted; font-size: 8.5pt; font-weight: 700; letter-spacing: 1px; }
        QLabel#bigTitle { font-size: 40pt; font-weight: 800; }
        QListWidget#grid { background: transparent; border: none; outline: none; }
        QLabel#pageTitle { font-size: 22pt; font-weight: 700; }
        QLabel#profileName { font-size: 34pt; font-weight: 800; }
        QLabel#tileName { font-weight: 600; }
        QPushButton#profileButton { background: transparent; border: none; border-radius: 18px; padding: 0; }
        QPushButton#profileButton:hover { background: @raisedHover; }
        QLabel#sectionTitle { font-size: 14pt; font-weight: 600; padding-top: 6px; }
        QWidget#statCard { background: @card; border-radius: 14px; }
        QLabel#statValue { font-size: 22pt; font-weight: 700; color: @accentText; }
        QLabel#statLabel { color: @muted; }
        QWidget#welcome { background: @card; border-radius: 14px; }
        QLabel#welcomeTitle { font-size: 15pt; font-weight: 700; color: @textBright; }
        QLabel#welcomeText { color: @textBody; }
        QPushButton#welcomeAction { background: @accent; border: none; border-radius: 17px; padding: 0 18px;
                                    min-height: 34px; max-height: 34px; color: @onAccent; font-weight: 600; }
        QPushButton#welcomeAction:hover { background: @accentHover; }
        QWidget#nowPlaying { background: @base; }
        QLabel#npTitle { font-size: 11pt; font-weight: 600; }
        QLabel#npSubtitle, QLabel#npTime { color: @muted; font-size: 9pt; }
        QLabel#npIcon { font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; color: @muted; font-size: 13pt; }
        QPushButton#transport, QPushButton#transportBig {
            font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; background: transparent; border: none;
            color: @textSoft; font-size: 14pt; padding: 4px;
        }
        QPushButton#transport:hover { color: @strong; }
        QPushButton#transportBig {
            background: @text; color: @window; border-radius: 18px;
            min-width: 36px; max-width: 36px; min-height: 36px; max-height: 36px; font-size: 13pt; padding: 0;
        }
        QPushButton#transportBig:hover { background: @strong; }
        QSlider::groove:horizontal { height: 4px; background: @track; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
        QSlider::handle:horizontal { background: @strong; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }
        QSlider::groove:horizontal:hover { background: @trackHover; }
        QPushButton {
            background: @card; border: 1px solid @line;
            border-radius: 10px; padding: 8px 16px; color: @text;
        }
        QPushButton:hover { background: @raised; }
        QLineEdit {
            background: @card; border: 1px solid @line;
            border-radius: 18px; padding: 0 16px; min-height: 36px; selection-background-color: @accent;
        }
        QLineEdit:focus { border-color: @accent; }
        QSplitter::handle { background: @base; }
        QSplitter::handle:hover { background: @select; }
        QScrollArea { background: transparent; }
        QScrollBar:vertical { background: transparent; width: 12px; margin: 4px 2px; border-radius: 5px; }

        QScrollBar::handle:vertical { background: @scroll; border-radius: 3px; min-height: 30px; margin: 2px 1px; }
        QScrollBar::handle:vertical:hover { background: @scrollHover; border-radius: 4px; margin: 1px 0px; }
        QScrollBar::handle:vertical:pressed { background: @accent; }
        QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px 4px; border-radius: 5px; }
        QLabel#seekTip { background: rgba(16,15,20,235); color: white; font-weight: 700; border-radius: 6px; padding: 2px 8px; }
        QScrollBar::handle:horizontal { background: @scroll; border-radius: 3px; min-width: 30px; margin: 1px 2px; }
        QScrollBar::handle:horizontal:hover { background: @scrollHover; border-radius: 4px; margin: 0px 1px; }
        QScrollBar::handle:horizontal:pressed { background: @accent; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QAbstractScrollArea::corner { background: transparent; border: none; }
        QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
        QMenu { background: @card; border: 1px solid @line; border-radius: 8px; padding: 4px; }
        QMenu::item { padding: 6px 18px; border-radius: 6px; }
        QMenu::item:selected { background: @select; }
    )"));
    // Point sizes and pixel lengths in the sheet follow the zoom too. Radii must shrink
    // with the boxes: Qt draws a square box once the radius is more than half its height.
    static const QRegularExpression unit(QStringLiteral(R"((\d+(?:\.\d+)?)(pt|px))"));
    QString scaled;
    qsizetype last = 0;
    for (auto it = unit.globalMatch(css); it.hasNext();) {
        const auto m = it.next();
        // The window's own controls (Home, search, settings, minimise...) stop
        // growing at 120%: past that they were taller than their strip.
        const qsizetype open = css.lastIndexOf(QLatin1Char('{'), m.capturedStart());
        const qsizetype close = css.lastIndexOf(QLatin1Char('}'), open);
        const QString selector = css.mid(close + 1, open - close - 1);
        const bool chrome = !selector.contains(QLatin1String("#glyph")) &&
            (selector.contains(QLatin1String("#pill")) || selector.contains(QLatin1String("#topSearch")) ||
             selector.contains(QLatin1String("#chrome")) || selector.contains(QLatin1String("#profileButton")));
        const qreal v = m.captured(1).toDouble() * (chrome ? chromeZoom() : g_zoom);
        // A radius rounds down, the box it is half of rounds to nearest: two
        // halves rounded up were wider than the box at some zooms (1.4: 24 + 24
        // over 47), and the button was drawn square.
        const qsizetype from = qMax(css.lastIndexOf(QLatin1Char(';'), m.capturedStart()),
                                    css.lastIndexOf(QLatin1Char('{'), m.capturedStart()));
        const QString property = css.mid(from, m.capturedStart() - from);
        const bool radius = property.contains(QLatin1String("radius"));
        // Border lines stay 1 px: at 2 px Qt left a hairline between a round
        // border and the background inside it, and the light palette showed
        // through around search boxes and chips at 160%.
        const bool line = !radius && property.contains(QLatin1String("border"));
        const QString n = m.captured(2) == QLatin1String("pt")
            ? QString::number(v, 'f', 1)
            : line ? m.captured(1)
            : QString::number(v <= 0 ? 0 : radius ? qMax(1, int(v)) : qMax(1, qRound(v)));
        scaled += css.mid(last, m.capturedStart() - last) + n + m.captured(2);
        last = m.capturedEnd();
    }
    app.setStyleSheet(scaled + css.mid(last));
    // Sheets set on single widgets, restyled with the new colours, and every
    // painted widget asked to draw again.
    for (QWidget *w : QApplication::allWidgets()) {
        const QVariant sheet = w->property(kSheetProperty);
        if (sheet.isValid())
            w->setStyleSheet(fill(sheet.toString()));
        w->update();
    }
    auto &list = refreshers();
    list.removeIf([](const auto &r) { return r.first.isNull(); });
    for (const auto &r : std::as_const(list))
        r.second();
}

void setZoom(QApplication &app, qreal factor)
{
    g_zoom = qBound(0.7, qRound(factor * 10) / 10.0, 1.6);   // tenths: 0.1 steps added up drift
    apply(app);
}

} // namespace theme
