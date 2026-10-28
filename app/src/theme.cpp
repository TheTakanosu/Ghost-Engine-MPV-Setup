#include "theme.h"

#include <QApplication>
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

namespace { qreal g_zoom = 1.0; }

qreal zoom() { return g_zoom; }

QColor window()       { return {22, 21, 27}; }
QColor card()         { return {33, 32, 41}; }
QColor cardHover()    { return {40, 39, 50}; }
QColor cardSelected() { return {46, 40, 62}; }
QColor accent()       { return {176, 127, 224}; }
QColor accentText()   { return {207, 179, 240}; }
QColor text()         { return {236, 236, 241}; }
QColor muted()        { return {154, 152, 166}; }

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
    app.setStyle(new ClickToJumpStyle(QStyleFactory::create("Fusion")));
    app.installEventFilter(new RoundMenus(&app));
    QFont font("Segoe UI");
    font.setPointSizeF(10 * g_zoom);
    app.setFont(font);

    QPalette p;
    p.setColor(QPalette::Window, window());
    p.setColor(QPalette::WindowText, text());
    p.setColor(QPalette::Base, window());
    p.setColor(QPalette::AlternateBase, card());
    p.setColor(QPalette::Text, text());
    p.setColor(QPalette::Button, card());
    p.setColor(QPalette::ButtonText, text());
    p.setColor(QPalette::Highlight, accent());
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, muted());
    p.setColor(QPalette::ToolTipBase, card());
    p.setColor(QPalette::ToolTipText, text());
    app.setPalette(p);

    QString css = QStringLiteral(R"(
        QWidget#central { background: rgb(12,11,15); }
        QWidget#topBar { background: rgb(12,11,15); }
        QWidget#libraryPanel, QWidget#panel { background: rgb(22,21,27); border-radius: 12px; }
        QWidget#home, QStackedWidget, QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }
        QPushButton#bigPlay {
            font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; background: rgb(176,127,224); border: none;
            color: rgb(24,18,34); border-radius: 27px; font-size: 15pt;
            min-width: 54px; max-width: 54px; min-height: 54px; max-height: 54px; padding: 0;
        }
        QPushButton#bigPlay:hover { background: rgb(196,152,240); }
        QPushButton#bigGlyph {
            font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; background: transparent; border: none;
            color: rgb(170,168,182); font-size: 17pt; padding: 4px;
        }
        QPushButton#bigGlyph:hover { color: white; }
        QWidget#columns { border-bottom: 1px solid rgb(44,42,54); }
        QLabel#columnLabel { color: rgb(154,152,166); font-size: 9pt; }
        QPushButton#link { background: transparent; border: none; color: rgb(176,127,224); padding: 0 4px; }
        QPushButton#link:hover { color: rgb(206,170,244); text-decoration: underline; }
        QLabel#npVolume { font-weight: 800; font-size: 9.5pt; color: rgb(236,236,241); }
        QLabel#settingName { font-size: 10.5pt; font-weight: 600; color: rgb(236,236,241); }
        QComboBox { background: rgb(40,39,50); border: 1px solid rgb(58,56,70); border-radius: 8px; padding: 6px 12px; color: rgb(236,236,241);
                    combobox-popup: 0; }
        QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 26px; border: none; background: transparent; }
        QComboBox::down-arrow { image: url(:/assets/chevron-down.png); width: 12px; height: 12px; }
        QComboBox:hover { border-color: rgb(176,127,224); }
        QComboBox QAbstractItemView { background: rgb(33,32,41); border: 1px solid rgb(52,50,64); border-radius: 8px;
                                      outline: none; padding: 4px; }
        QComboBox QAbstractItemView::item { padding: 6px 14px; border-radius: 6px; color: rgb(236,236,241); min-height: 20px; }
        QComboBox QAbstractItemView::item:selected, QComboBox QAbstractItemView::item:hover { background: rgb(60,48,84); }
        QComboBoxPrivateContainer { background: transparent; border: none; }
        QLabel#toast { background: rgb(236,236,241); color: rgb(24,18,34); border-radius: 16px; font-weight: 600; }
        QPushButton#columnButton, QPushButton#clockHeader {
            background: transparent; border: none; padding: 0; color: rgb(154,152,166); font-size: 9pt; text-align: left;
        }
        QPushButton#columnButton:hover { color: rgb(236,236,241); }
        QPushButton#columnButton[active="true"] { color: rgb(236,236,241); font-weight: 600; }
        QLineEdit#nameEdit { background: rgba(0,0,0,90); border: 1px solid rgb(176,127,224); border-radius: 8px;
                             padding: 0 6px; font-weight: 800; color: white; }
        QLineEdit#songSearch { border-radius: 16px; padding: 0 14px; min-height: 32px; max-height: 32px; }
        QLabel#heroInfo { color: rgb(225,223,232); font-size: 10pt; }
        QLabel#panelTitle { font-size: 11.5pt; font-weight: 700; color: rgb(220,218,228); }
        QListWidget#library, QListWidget#songs { background: transparent; border: none; outline: none; }
        QPushButton#glyph, QPushButton#pill, QPushButton#chrome, QPushButton#chromeClose {
            font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; background: transparent; border: none;
            color: rgb(170,168,182); font-size: 11pt; padding: 6px; border-radius: 8px;
        }
        QPushButton#glyph:hover { color: white; background: rgb(40,39,50); }
        QPushButton#pill {
            background: rgb(33,32,41); color: rgb(236,236,241); border-radius: 17px;
            min-width: 34px; max-width: 34px; min-height: 34px; max-height: 34px; padding: 0; font-size: 12pt;
        }
        QPushButton#pill:hover { background: rgb(46,44,58); }
        QPushButton#chrome, QPushButton#chromeClose {
            border-radius: 0; min-width: 46px; max-width: 46px; min-height: 44px; max-height: 44px; padding: 0; font-size: 9pt;
        }
        QPushButton#chrome:hover { background: rgb(40,39,50); color: white; }
        QPushButton#chromeClose:hover { background: rgb(196,43,28); color: white; }
        QLineEdit#topSearch { background: rgb(33,32,41); border-radius: 17px; padding: 0 16px; min-height: 34px; max-height: 34px; }
        QPushButton#chip { border-radius: 14px; padding: 0 12px; min-height: 28px; max-height: 28px; font-size: 9pt; }
        QPushButton#chip:checked { background: rgb(236,236,241); border-color: rgb(236,236,241); color: rgb(22,21,27); }
        QPushButton#primary {
            background: rgb(176,127,224); border: none; color: rgb(24,18,34); font-weight: 700;
            border-radius: 20px; padding: 0 28px; min-height: 40px; max-height: 40px;
        }
        QPushButton#primary:hover { background: rgb(192,148,236); }
        QLabel#kicker { color: rgb(154,152,166); font-size: 8.5pt; font-weight: 700; letter-spacing: 1px; }
        QLabel#bigTitle { font-size: 40pt; font-weight: 800; }
        QListWidget#grid { background: transparent; border: none; outline: none; }
        QLabel#pageTitle { font-size: 22pt; font-weight: 700; }
        QLabel#sectionTitle { font-size: 14pt; font-weight: 600; padding-top: 6px; }
        QWidget#statCard { background: rgb(33,32,41); border-radius: 14px; }
        QLabel#statValue { font-size: 22pt; font-weight: 700; color: rgb(207,179,240); }
        QLabel#statLabel { color: rgb(154,152,166); }
        QWidget#nowPlaying { background: rgb(12,11,15); }
        QLabel#npTitle { font-size: 11pt; font-weight: 600; }
        QLabel#npSubtitle, QLabel#npTime { color: rgb(154,152,166); font-size: 9pt; }
        QLabel#npIcon { font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; color: rgb(154,152,166); font-size: 13pt; }
        QPushButton#transport, QPushButton#transportBig {
            font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; background: transparent; border: none;
            color: rgb(220,218,228); font-size: 14pt; padding: 4px;
        }
        QPushButton#transport:hover { color: white; }
        QPushButton#transportBig {
            background: rgb(236,236,241); color: rgb(22,21,27); border-radius: 18px;
            min-width: 36px; max-width: 36px; min-height: 36px; max-height: 36px; font-size: 13pt; padding: 0;
        }
        QPushButton#transportBig:hover { background: white; }
        QSlider::groove:horizontal { height: 4px; background: rgb(60,58,72); border-radius: 2px; }
        QSlider::sub-page:horizontal { background: rgb(176,127,224); border-radius: 2px; }
        QSlider::handle:horizontal { background: white; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }
        QSlider::groove:horizontal:hover { background: rgb(76,74,90); }
        QPushButton {
            background: rgb(33,32,41); border: 1px solid rgb(52,50,64);
            border-radius: 10px; padding: 8px 16px; color: rgb(236,236,241);
        }
        QPushButton:hover { background: rgb(42,40,53); }
        QLineEdit {
            background: rgb(33,32,41); border: 1px solid rgb(52,50,64);
            border-radius: 18px; padding: 0 16px; min-height: 36px; selection-background-color: rgb(176,127,224);
        }
        QLineEdit:focus { border-color: rgb(176,127,224); }
        QSplitter::handle { background: rgb(12,11,15); }
        QSplitter::handle:hover { background: rgb(60,48,84); }
        QScrollArea { background: transparent; }
        QScrollBar:vertical { background: transparent; width: 12px; margin: 4px 2px; border-radius: 5px; }
        QScrollBar:vertical:hover { background: rgba(255,255,255,10); }
        QScrollBar::handle:vertical { background: rgb(60,58,72); border-radius: 3px; min-height: 30px; margin: 2px 3px; }
        QScrollBar::handle:vertical:hover { background: rgb(96,92,112); border-radius: 4px; margin: 1px 2px; }
        QScrollBar::handle:vertical:pressed { background: rgb(176,127,224); }
        QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px 4px; border-radius: 5px; }
        QScrollBar:horizontal:hover { background: rgba(255,255,255,10); }
        QScrollBar::handle:horizontal { background: rgb(60,58,72); border-radius: 3px; min-width: 30px; margin: 3px 2px; }
        QScrollBar::handle:horizontal:hover { background: rgb(96,92,112); border-radius: 4px; margin: 2px 1px; }
        QScrollBar::handle:horizontal:pressed { background: rgb(176,127,224); }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
        QMenu { background: rgb(33,32,41); border: 1px solid rgb(52,50,64); border-radius: 8px; padding: 4px; }
        QMenu::item { padding: 6px 18px; border-radius: 6px; }
        QMenu::item:selected { background: rgb(60,48,84); }
    )");
    // Point sizes and pixel lengths in the sheet follow the zoom too. Radii must shrink
    // with the boxes: Qt draws a square box once the radius is more than half its height.
    static const QRegularExpression unit(QStringLiteral(R"((\d+(?:\.\d+)?)(pt|px))"));
    QString scaled;
    qsizetype last = 0;
    for (auto it = unit.globalMatch(css); it.hasNext();) {
        const auto m = it.next();
        const qreal v = m.captured(1).toDouble() * g_zoom;
        const QString n = m.captured(2) == QLatin1String("pt")
            ? QString::number(v, 'f', 1)
            : QString::number(v > 0 ? qMax(1, qRound(v)) : 0);
        scaled += css.mid(last, m.capturedStart() - last) + n + m.captured(2);
        last = m.capturedEnd();
    }
    app.setStyleSheet(scaled + css.mid(last));
}

void setZoom(QApplication &app, qreal factor)
{
    g_zoom = qBound(0.7, factor, 1.6);
    apply(app);
}

} // namespace theme
