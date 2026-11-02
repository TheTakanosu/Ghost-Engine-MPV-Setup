#include "traymenu.h"

#include "icons.h"

#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include <windows.h>

namespace {

const QColor kCard(30, 29, 37);
const QColor kHairline(255, 255, 255, 26);

QFrame *line()
{
    auto *rule = new QFrame;
    rule->setFixedHeight(1);
    rule->setStyleSheet("background: rgba(255,255,255,22); border: none;");
    return rule;
}

// A row of the menu: the whole width is the button, lit on hover.
QPushButton *row(const QString &label)
{
    auto *button = new QPushButton(label);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(
        "QPushButton { text-align: left; padding: 0 14px; min-height: 34px; max-height: 34px; border: none;"
        " border-radius: 7px; background: transparent; color: rgb(240,240,245); font-size: 10.5pt; font-weight: 600; }"
        "QPushButton:hover { background: rgba(255,255,255,18); }");
    return button;
}

// The playback buttons, drawn by the app's own icon painter: glyphs from an
// icon font came out as dots in this popup.
icons::IconButton *glyph(icons::Icon icon, const QString &tip)
{
    auto *button = new icons::IconButton(icon, 32, tip);
    button->setColors(QColor(220, 218, 230), Qt::white);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

// The tick by "Keep running in the tray", drawn for the same reason.
QPixmap tickMark(qreal dpr)
{
    QPixmap mark(QSize(14, 14) * dpr);
    mark.setDevicePixelRatio(dpr);
    mark.fill(Qt::transparent);
    QPainter p(&mark);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(240, 240, 245), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPolyline(QPolygonF{{2.5, 7.4}, {5.6, 10.4}, {11.6, 3.8}});
    return mark;
}

} // namespace

TrayMenu::TrayMenu(QWidget *parent) : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(270);

    m_now = new QLabel;
    m_now->setStyleSheet("color: rgb(176,174,188); font-size: 9.5pt; background: transparent; padding: 0 14px;");
    m_now->setMinimumWidth(0);

    auto *previous = glyph(icons::Icon::Previous, "Previous");
    m_pause = glyph(icons::Icon::Pause, "Pause");
    auto *next = glyph(icons::Icon::Next, "Next");
    auto *controls = new QHBoxLayout;
    controls->setContentsMargins(8, 0, 8, 0);
    controls->setSpacing(2);
    controls->addWidget(previous);
    controls->addWidget(m_pause);
    controls->addWidget(next);
    controls->addStretch();

    auto *open = row("Open Ghost Engine");
    auto *keep = row("Keep running in the tray");
    keep->setToolTip("Closing the window keeps Ghost Engine and the music going here.");
    m_keepTick = new QLabel;
    m_keepTick->setPixmap(tickMark(devicePixelRatioF()));
    m_keepTick->setStyleSheet("background: transparent;");
    m_keepTick->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *tickRow = new QHBoxLayout(keep);
    tickRow->setContentsMargins(0, 0, 14, 0);
    tickRow->addStretch();
    tickRow->addWidget(m_keepTick);
    auto *updates = row("Check for updates...");
    auto *thanks = row("Thanks to...");
    auto *quit = row("Quit Ghost Engine");

    auto *box = new QVBoxLayout(this);
    box->setContentsMargins(6, 10, 6, 6);
    box->setSpacing(2);
    box->addWidget(m_now);
    box->addSpacing(4);
    box->addLayout(controls);
    box->addSpacing(6);
    for (QWidget *w : std::initializer_list<QWidget *>{line(), open, keep, line(), updates, thanks, line(), quit}) {
        if (qobject_cast<QFrame *>(w)) {   // a rule, with air either side
            box->addSpacing(4);
            box->addWidget(w);
            box->addSpacing(4);
        } else {
            box->addWidget(w);
        }
    }

    // Each closes the menu first, as a menu does.
    const auto act = [this](QAbstractButton *button, std::function<void()> TrayMenu::*what, bool closes) {
        connect(button, &QAbstractButton::clicked, this, [this, what, closes] {
            if (closes)
                hide();
            if (this->*what)
                (this->*what)();
        });
    };
    act(previous, &TrayMenu::onPrevious, false);
    act(m_pause, &TrayMenu::onPlayPause, false);
    act(next, &TrayMenu::onNext, false);
    act(open, &TrayMenu::onOpen, true);
    act(updates, &TrayMenu::onCheckUpdates, true);
    act(thanks, &TrayMenu::onThanks, true);
    act(quit, &TrayMenu::onQuit, true);
    connect(keep, &QPushButton::clicked, this, [this] {
        setKeepRunning(!m_keepRunning);
        if (onKeepRunning)
            onKeepRunning(m_keepRunning);
    });
    setNowPlaying(QString(), QString(), true);
}

void TrayMenu::setNowPlaying(const QString &title, const QString &artist, bool paused)
{
    const QString shown = title.isEmpty() ? QStringLiteral("Nothing playing")
                                          : (artist.isEmpty() ? title : title + QStringLiteral(" · ") + artist);
    m_now->setToolTip(title.isEmpty() ? QString() : shown);
    m_now->setText(m_now->fontMetrics().elidedText(shown, Qt::ElideRight, width() - 12 - 28));
    m_pause->setIcon(paused ? icons::Icon::Play : icons::Icon::Pause);
    m_pause->setToolTip(paused ? "Play" : "Pause");
}

void TrayMenu::setKeepRunning(bool on)
{
    m_keepRunning = on;
    m_keepTick->setVisible(on);
}

void TrayMenu::popupAt(const QPoint &cursor)
{
    adjustSize();
    // Up and to the left of the cursor, the way Windows opens a tray menu,
    // then pulled inside the screen whatever side the taskbar is on.
    const QScreen *screen = QGuiApplication::screenAt(cursor);
    const QRect room = screen ? screen->availableGeometry() : QRect(cursor - QPoint(1000, 1000), QSize(2000, 2000));
    QPoint at = cursor - QPoint(width(), height());
    at.setX(qBound(room.left() + 4, at.x(), room.right() - width() - 4));
    at.setY(qBound(room.top() + 4, at.y(), room.bottom() - height() - 4));
    move(at);
    show();
    raise();
    // A tray click leaves the taskbar in front. Windows asks a tray menu to
    // come forward itself (as it does of TrackPopupMenu), so that it takes the
    // keyboard and closes on a click anywhere else; the click gave the right.
    SetForegroundWindow(reinterpret_cast<HWND>(winId()));
    activateWindow();
}

void TrayMenu::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(kHairline, 1));
    p.setBrush(kCard);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
}
