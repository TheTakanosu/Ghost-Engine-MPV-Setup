#include "videopage.h"

#include "icons.h"
#include "theme.h"
#include "tiles.h"

#include <QDesktopServices>
#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScreen>
#include <QScrollBar>
#include <QSlider>
#include <QStyledItemDelegate>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <cmath>

#include <windows.h>
#include <dwmapi.h>

namespace {

constexpr int kSideWidth = 400;          // the list on the right
constexpr int kTargetRole = Qt::UserRole + 40;
constexpr int kNowRole = Qt::UserRole + 41;
constexpr int kCollapsedLines = 3;       // of the description, before "...more"
const int kHeights[] = {0, 2160, 1440, 1080, 720, 480, 360};
const double kSpeeds[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
const QColor kGlyph(236, 236, 241);

QString clock(double seconds)
{
    if (seconds < 0)
        return QStringLiteral("--:--");
    const int s = int(seconds);
    return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// The description as YouTube shows it: links, timestamps that jump the
// video there, and hashtags, all clickable.
QString linkify(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral(R"((https?://[^\s<]+)|(?<![\w:])((?:\d{1,2}:)?\d{1,2}:\d{2})(?![\w:])|(?<![\w&/])(#[\p{L}\p{N}_]+))"),
        QRegularExpression::UseUnicodePropertiesOption);
    const QString escaped = text.toHtmlEscaped();
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(escaped);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += escaped.mid(last, m.capturedStart() - last);
        if (!m.captured(1).isEmpty()) {
            out += QStringLiteral("<a href=\"%1\">%1</a>").arg(m.captured(1));
        } else if (!m.captured(2).isEmpty()) {
            int seconds = 0;
            for (const QString &part : m.captured(2).split(':'))
                seconds = seconds * 60 + part.toInt();
            out += QStringLiteral("<a href=\"seek:%1\">%2</a>").arg(seconds).arg(m.captured(2));
        } else {
            const QString tag = m.captured(3);
            out += QStringLiteral("<a href=\"https://www.youtube.com/hashtag/%1\">%2</a>").arg(tag.mid(1), tag);
        }
        last = m.capturedEnd();
    }
    out += escaped.mid(last);
    out.replace('\n', QStringLiteral("<br>"));
    return out;
}

// A see-through window of its own, kept over the picture by the main window
// that owns it: nothing the app paints can be on top of mpv's window.
class Floating : public QWidget {
public:
    Floating()
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }
    int corner = 12;   // the picture's, so nothing shows past its round corners
};

// The controls over the picture's bottom edge, on a shade that fades into
// the video, as YouTube's do.
class ControlsBar : public Floating {
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(rect()).adjusted(0, -corner, 0, 0), corner, corner);
        QLinearGradient shade(0, 0, 0, height());
        shade.setColorAt(0, QColor(0, 0, 0, 0));
        shade.setColorAt(0.4, QColor(0, 0, 0, 130));
        shade.setColorAt(1, QColor(0, 0, 0, 215));
        p.fillPath(shape, shade);
    }
};

// Laid over the visible part of the picture. Its corners are the page's
// colour, cut round with antialiasing (the window region under it can only
// cut in whole pixels, which left a stair of black dots). The rest is all
// but clear, so the mouse comes here rather than to mpv: a click pauses, a
// double click goes full screen, the wheel scrolls the page, as on YouTube.
class CornerMask : public Floating {
public:
    QColor fill{22, 21, 27};   // the panel the page sits on
    QRect picture;             // the whole picture, in this window's coordinates
    std::function<void()> clicked;
    std::function<void()> doubleClicked;
    std::function<void(QWheelEvent *)> wheeled;

    void reshape() { update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(0, 0, 0, 1));
        if (corner <= 0)
            return;
        QPainterPath outside;
        outside.addRect(QRectF(rect()));
        QPainterPath shape;
        shape.addRoundedRect(QRectF(picture.isEmpty() ? rect() : picture), corner, corner);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillPath(outside.subtracted(shape), fill);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && !m_double && clicked)
            clicked();
        m_double = false;
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        m_double = true;
        if (doubleClicked)
            doubleClicked();
    }
    void wheelEvent(QWheelEvent *event) override
    {
        if (wheeled)
            wheeled(event);
    }

private:
    bool m_double = false;
};

// The mini player: a note window of its own, so it can be dragged
// anywhere on the screen. mpv's surface is moved into it; Windows 11
// rounds its corners smoothly. Its face, a window of its own on top,
// moves with it.
class MiniWindow : public QWidget {
public:
    MiniWindow() : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, Qt::black);
        setPalette(p);
    }
    QWidget *face = nullptr;
    double aspect = 16.0 / 9.0;
    bool pinned = false;
    std::function<void(const QSize &)> resized;

    // Larger or smaller around its middle, the video's shape kept.
    void zoomBy(double factor)
    {
        const QRect screen = this->screen() ? this->screen()->availableGeometry() : QRect(0, 0, 1920, 1080);
        const int w = qBound(220, int(width() * factor), int(screen.width() * 0.9));
        const QSize size(w, int(w / aspect));
        QRect r(QPoint(), size);
        r.moveCenter(geometry().center());
        setGeometry(r);
    }
    // Pinned, it stays above every program's windows and is no longer the
    // main window's: Windows would hide it with the main window.
    void applyPin(QWidget *owner)
    {
        // Through Qt's own window setting: Qt puts back the stacking it knows
        // of whenever it shows the window. It keeps the main window as the
        // owner too; WM_SHOWWINDOW below keeps a pinned one up anyway.
        Q_UNUSED(owner);
        winId();
        for (QWidget *w : {static_cast<QWidget *>(this), face})
            if (w && w->windowHandle())
                w->windowHandle()->setFlag(Qt::WindowStaysOnTopHint, pinned);
        addFrame();
    }
    // A sizing frame, so Windows can resize it; none of it is drawn, the
    // whole window is client area.
    void addFrame()
    {
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        if (!(style & WS_THICKFRAME)) {
            SetWindowLongW(hwnd, GWL_STYLE, style | WS_THICKFRAME);
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }

protected:
    void showEvent(QShowEvent *) override
    {
        addFrame();
        const int round = 2;   // DWMWA_WINDOW_CORNER_PREFERENCE: round
        DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), 33, &round, sizeof(round));
        face->show();
        follow();
    }
    void hideEvent(QHideEvent *) override { face->hide(); }
    void moveEvent(QMoveEvent *) override { follow(); }
    void resizeEvent(QResizeEvent *) override
    {
        follow();
        if (resized)
            resized(size());
    }
    bool nativeEvent(const QByteArray &, void *message, qintptr *result) override
    {
        const MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_SHOWWINDOW && !msg->wParam && msg->lParam == SW_PARENTCLOSING && pinned) {
            // The main window is being minimised: Windows hides the windows
            // it owns. Pinned, the mini player stays.
            *result = 0;
            return true;
        }
        if (msg->message == WM_NCCALCSIZE && msg->wParam) {
            *result = 0;   // no frame drawn: all of it is the picture
            return true;
        }
        if (msg->message == WM_SIZING) {
            // Kept at the video's shape whichever edge is dragged.
            RECT *r = reinterpret_cast<RECT *>(msg->lParam);
            const int edge = int(msg->wParam);
            const qreal dpr = devicePixelRatioF();
            const int minWidth = int(220 * dpr);
            if (r->right - r->left < minWidth) {
                if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT)
                    r->left = r->right - minWidth;
                else
                    r->right = r->left + minWidth;
            }
            if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM) {
                r->right = r->left + int((r->bottom - r->top) * aspect);
            } else {
                const int h = int((r->right - r->left) / aspect);
                if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
                    r->top = r->bottom - h;
                else
                    r->bottom = r->top + h;
            }
            *result = TRUE;
            return true;
        }
        return false;
    }

private:
    void follow()
    {
        // Its face stays on top of it: Windows put the mini window back
        // over it after its frame was changed, hiding the buttons.
        if (face) {
            face->setGeometry(geometry());
            if (face->isVisible())
                SetWindowPos(reinterpret_cast<HWND>(face->winId()), pinned ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
};

// The mini player's face: clicked, it goes back to the video; dragged, it
// takes the mini player along; with the mouse on it, it darkens and shows
// its buttons. Never quite clear, or clicks would fall through to mpv.
class MiniFace : public Floating {
public:
    std::function<void()> clicked;
    std::function<void(const QPoint &)> moved;
    QWidget *carry = nullptr;   // the mini player's window
    QList<QWidget *> buttons;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(rect()), corner, corner);
        p.fillPath(shape, QColor(0, 0, 0, m_hot ? 120 : 1));
        if (m_hot) {
            p.setPen(QColor(255, 255, 255, 40));
            p.drawPath(shape);
        }
    }
    void enterEvent(QEnterEvent *) override { setHot(true); }
    void leaveEvent(QEvent *) override { setHot(false); }
    // Which edge the mouse is on, as WM_SIZING names them; 0: none.
    int edgeAt(const QPoint &at) const
    {
        const int band = 8;
        const bool left = at.x() < band, right = at.x() >= width() - band;
        const bool top = at.y() < band, bottom = at.y() >= height() - band;
        if (top && left) return WMSZ_TOPLEFT;
        if (top && right) return WMSZ_TOPRIGHT;
        if (bottom && left) return WMSZ_BOTTOMLEFT;
        if (bottom && right) return WMSZ_BOTTOMRIGHT;
        if (left) return WMSZ_LEFT;
        if (right) return WMSZ_RIGHT;
        if (top) return WMSZ_TOP;
        if (bottom) return WMSZ_BOTTOM;
        return 0;
    }
    // Windows' own move and size loops: as smooth as any window's.
    void runLoop(WPARAM command)
    {
        if (!carry)
            return;
        ReleaseCapture();
        SendMessageW(reinterpret_cast<HWND>(carry->winId()), WM_SYSCOMMAND, command, 0);
        if (moved)
            moved(carry->pos());
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        m_press = event->globalPosition().toPoint();
        m_pressed = true;
        if (const int edge = edgeAt(event->position().toPoint())) {
            m_pressed = false;
            runLoop(SC_SIZE | WPARAM(edge));
        }
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton)) {
            static const Qt::CursorShape shapes[] = {Qt::ArrowCursor, Qt::SizeHorCursor, Qt::SizeHorCursor,
                Qt::SizeVerCursor, Qt::SizeFDiagCursor, Qt::SizeBDiagCursor, Qt::SizeVerCursor,
                Qt::SizeBDiagCursor, Qt::SizeFDiagCursor};
            setCursor(shapes[edgeAt(event->position().toPoint())]);
            return;
        }
        if (!m_pressed || (event->globalPosition().toPoint() - m_press).manhattanLength() < 5)
            return;
        m_pressed = false;
        runLoop(SC_MOVE | HTCAPTION);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        if (m_pressed && rect().contains(event->position().toPoint())) {
            // A button that had not shown yet (the mouse came in too fast for
            // the hover) is still the one clicked, not the video behind it.
            for (QWidget *b : std::as_const(buttons))
                if (b->geometry().contains(event->position().toPoint())) {
                    m_pressed = false;
                    setHot(true);
                    if (auto *button = qobject_cast<QAbstractButton *>(b))
                        button->click();
                    return;
                }
            if (clicked)
                clicked();
        }
        m_pressed = false;
    }
    void resizeEvent(QResizeEvent *) override { placeButtons(); }
    void wheelEvent(QWheelEvent *event) override
    {
        if (zoomed && event->angleDelta().y() != 0)
            zoomed(std::pow(1.1, event->angleDelta().y() / 120.0));
    }
    bool nativeEvent(const QByteArray &, void *message, qintptr *result) override
    {
        // The main window is being minimised: Windows hides every window in
        // its chain, this one too. Pinned, the buttons stay with the picture.
        const MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_SHOWWINDOW && !msg->wParam && msg->lParam == SW_PARENTCLOSING && carry
            && static_cast<MiniWindow *>(carry)->pinned) {
            *result = 0;
            return true;
        }
        return false;
    }

public:
    std::function<void(double)> zoomed;

private:
    QPoint m_press;
    bool m_pressed = false;
    // Play in the middle; back to the video and pin at the top left, close
    // at the top right.
    void placeButtons()
    {
        if (buttons.size() < 4)
            return;
        buttons[0]->move((width() - buttons[0]->width()) / 2, (height() - buttons[0]->height()) / 2);
        buttons[1]->move(8, 8);
        buttons[2]->move(8 + buttons[1]->width() + 4, 8);
        buttons[3]->move(width() - buttons[3]->width() - 8, 8);
    }
    void setHot(bool hot)
    {
        m_hot = hot;
        placeButtons();
        for (QWidget *b : std::as_const(buttons))
            b->setVisible(hot);
        update();
    }
    bool m_hot = false;
};

// A video in the list on the right: its thumbnail, title, and length, as in
// YouTube's "up next".
class VideoRow : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    std::function<QPixmap(const QString &)> cover;
    std::function<QString(const QString &)> length;
    std::function<QString(const QString &)> channel;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return {100, int(84 * theme::zoom())};   // as wide as the list
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(2, 3, -2, -3);
        const bool now = index.data(kNowRole).toBool();
        const bool hover = option.state & QStyle::State_MouseOver;
        if (now || hover) {
            QPainterPath bg;
            bg.addRoundedRect(row, 8, 8);
            p->fillPath(bg, now ? theme::cardSelected() : theme::cardHover());
        }
        const QString target = index.data(kTargetRole).toString();
        const QRectF thumb(row.left() + 6, row.top() + 5, (row.height() - 10) * 16 / 9, row.height() - 10);
        tiles::paintCover(*p, thumb, cover ? cover(target) : QPixmap(), index.data(Qt::DisplayRole).toString(), 6);
        const QString len = length ? length(target) : QString();
        if (!len.isEmpty()) {
            QFont f = option.font;
            f.setPointSizeF(f.pointSizeF() * 0.85);
            f.setWeight(QFont::DemiBold);
            const QFontMetricsF fm(f);
            const QRectF badge(thumb.right() - fm.horizontalAdvance(len) - 12, thumb.bottom() - fm.height() - 6,
                               fm.horizontalAdvance(len) + 8, fm.height() + 2);
            QPainterPath b;
            b.addRoundedRect(badge, 4, 4);
            p->fillPath(b, QColor(0, 0, 0, 190));
            p->setFont(f);
            p->setPen(Qt::white);
            p->drawText(badge, Qt::AlignCenter, len);
        }
        const QRectF text(thumb.right() + 10, row.top() + 6, row.right() - thumb.right() - 16, row.height() - 12);
        QFont title = option.font;
        title.setWeight(QFont::DemiBold);
        p->setFont(title);
        p->setPen(now ? theme::accent() : theme::text());
        const QFontMetricsF tm(title);
        const QRectF titleBox(text.left(), text.top(), text.width(), tm.lineSpacing() * 2);
        const QString shown = tm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, titleBox.width() * 2 - 20);
        p->drawText(titleBox, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, shown);
        // The channel goes right under the title, on its second line when
        // the title takes only one.
        const qreal used = qMin(titleBox.height(),
                                tm.boundingRect(titleBox, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, shown).height());
        const QString by = channel ? channel(target) : QString();
        if (!by.isEmpty()) {
            QFont note = option.font;
            note.setPointSizeF(note.pointSizeF() * 0.9);
            p->setFont(note);
            p->setPen(QColor(154, 152, 166));
            p->drawText(QRectF(text.left(), titleBox.top() + used + 1, text.width(), QFontMetricsF(note).lineSpacing()),
                        Qt::AlignLeft | Qt::AlignVCenter, QFontMetricsF(note).elidedText(by, Qt::ElideRight, text.width()));
        }
        p->restore();
    }
};

QPushButton *action(const QString &glyph, const QString &label)
{
    auto *b = new QPushButton(glyph + QStringLiteral("  ") + label);
    b->setObjectName("videoAction");
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// A button over the video: its glyph or text, and on hover a soft round
// behind it the size of the button, a circle for glyphs and a pill for text.
class FlatButton : public QPushButton {
public:
    FlatButton(const QString &text, bool glyph, int size, qreal pointSize)
        : QPushButton(text), m_glyph(glyph), m_size(size), m_point(pointSize)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFlat(true);
    }
    void setOn(bool on) { m_on = on; update(); }
    void setPip(bool pip) { m_pip = pip; update(); }   // a small screen in a big one

    QSize sizeHint() const override
    {
        if (m_glyph)
            return {m_size, m_size};
        QFont f = font();
        f.setPointSizeF(m_point);
        f.setWeight(QFont::DemiBold);
        return {int(QFontMetricsF(f).horizontalAdvance(text())) + 18, m_size};
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
        if (underMouse() || isDown()) {
            QPainterPath round;
            round.addRoundedRect(box, box.height() / 2, box.height() / 2);
            p.fillPath(round, QColor(255, 255, 255, isDown() ? 46 : 30));
        }
        QFont f = font();
        if (m_glyph)
            f.setFamilies({QStringLiteral("Segoe Fluent Icons"), QStringLiteral("Segoe MDL2 Assets")});
        f.setPointSizeF(m_point);
        if (!m_glyph)
            f.setWeight(QFont::DemiBold);
        p.setFont(f);
        const QColor ink = m_on ? theme::accent() : QColor(240, 240, 245);
        if (m_pip) {
            const qreal w = height() * 0.5, h = w * 0.72;
            const QRectF screen(box.center().x() - w / 2, box.center().y() - h / 2, w, h);
            p.setPen(QPen(ink, 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(screen, 2, 2);
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            p.drawRoundedRect(QRectF(screen.center().x() + 0.5, screen.center().y() + 0.5,
                                     screen.width() / 2 - 2.5, screen.height() / 2 - 2.5), 1, 1);
            return;
        }
        p.setPen(ink);
        p.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    bool m_glyph;
    int m_size;
    qreal m_point;
    bool m_on = false;
    bool m_pip = false;
};

QPushButton *glyphButton(const QString &glyph, const QString &tip, qreal pointSize = 10.5)
{
    auto *b = new FlatButton(glyph, true, 30, pointSize);
    b->setToolTip(tip);
    return b;
}

QPushButton *textButton(const QString &text, const QString &tip)
{
    auto *b = new FlatButton(text, false, 30, 9.5);
    b->setToolTip(tip);
    return b;
}

QPushButton *miniButton(const QString &glyph, const QString &tip)
{
    auto *b = new FlatButton(glyph, true, 26, 8.5);
    b->setToolTip(tip);
    b->setFixedSize(b->sizeHint());   // no layout places these: they keep their size
    return b;
}

icons::IconButton *iconButton(icons::Icon icon, int size, const QString &tip)
{
    auto *b = new icons::IconButton(icon, size, tip);
    b->setColors(kGlyph, Qt::white);
    return b;
}

} // namespace

NativeSurface::NativeSurface(QWidget *window)
{
    static const wchar_t *kClass = L"GhostEngineVideo";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        // The panels' colour: while the picture is being made larger, mpv
        // draws the new part a moment later, and black there stood out.
        wc.hbrBackground = CreateSolidBrush(RGB(22, 21, 27));
        RegisterClassW(&wc);
        registered = true;
    }
    m_hwnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 1, 1,
                             reinterpret_cast<HWND>(window->winId()), nullptr, GetModuleHandleW(nullptr), nullptr);
    m_dpr = window->devicePixelRatioF();
}

void NativeSurface::place(QWidget *window, const QRect &rect)
{
    // mpv's window goes into the mini player's window and back: the same
    // handle, given another parent, so mpv never notices.
    const HWND hwnd = static_cast<HWND>(m_hwnd);
    const HWND parent = reinterpret_cast<HWND>(window->winId());
    if (GetParent(hwnd) != parent)
        SetParent(hwnd, parent);
    const qreal dpr = window->devicePixelRatioF();
    const bool resized = rect.size() != m_rect.size() || dpr != m_dpr;
    m_rect = rect;
    m_dpr = dpr;
    // The cut first, then the move: the other way round, for a moment the
    // old cut stood at the new place.
    if (resized)
        clip();
    SetWindowPos(hwnd, HWND_TOP, int(rect.x() * dpr), int(rect.y() * dpr), int(rect.width() * dpr),
                 int(rect.height() * dpr), SWP_NOACTIVATE);
}

void NativeSurface::setCorner(int radius)
{
    if (radius == m_corner)
        return;
    m_corner = radius;
    clip();
}

void NativeSurface::setVisibleArea(const QRect &area)
{
    // The page scrolls the picture up under the top bar, as YouTube's does;
    // only its part inside the page shows.
    if (area == m_visible)
        return;
    m_visible = area;
    clip();
}

void NativeSurface::setVisible(bool visible)
{
    ShowWindow(static_cast<HWND>(m_hwnd), visible ? SW_SHOWNA : SW_HIDE);
}

void NativeSurface::keepOnTop()
{
    const HWND hwnd = static_cast<HWND>(m_hwnd);
    if (IsWindowVisible(hwnd) && GetWindow(GetParent(hwnd), GW_CHILD) != hwnd)
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void NativeSurface::clip()
{
    // Round corners a little tighter than the round the corner mask paints
    // over them, so the picture reaches under its smooth edge everywhere.
    const qreal dpr = m_dpr;
    const int w = int(m_rect.width() * dpr), h = int(m_rect.height() * dpr), r = int(2 * m_corner * 0.6 * dpr);
    HRGN shape = r > 0 ? CreateRoundRectRgn(0, 0, w + 1, h + 1, r, r) : CreateRectRgn(0, 0, w, h);
    if (!m_visible.isEmpty()) {
        HRGN shown = CreateRectRgn(int(m_visible.left() * dpr), int(m_visible.top() * dpr),
                                   int((m_visible.right() + 1) * dpr), int((m_visible.bottom() + 1) * dpr));
        CombineRgn(shape, shape, shown, RGN_AND);
        DeleteObject(shown);
    }
    SetWindowRgn(static_cast<HWND>(m_hwnd), shape, TRUE);
}

VideoPage::VideoPage(QWidget *parent) : QWidget(parent)
{
    // The player area: the picture's place, centred at the video's shape so
    // the page shows around it rather than black bars.
    m_player = new QWidget;
    m_player->setMinimumWidth(320);
    m_player->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_player->installEventFilter(this);
    m_screen = new QWidget(m_player);
    m_screen->setObjectName("videoScreen");
    m_screen->setAttribute(Qt::WA_StyledBackground);
    m_screen->installEventFilter(this);

    // While mpv plays in its own window, this stands where the picture was.
    m_away = new QWidget(m_player);
    m_away->setObjectName("videoAway");
    m_away->setAttribute(Qt::WA_StyledBackground);
    m_away->hide();
    auto *awayText = new QLabel("Playing in mpv's own window");
    awayText->setObjectName("sectionTitle");
    awayText->setAlignment(Qt::AlignCenter);
    auto *back = new QPushButton("Bring it back here");
    back->setObjectName("primary");
    back->setCursor(Qt::PointingHandCursor);
    connect(back, &QPushButton::clicked, this, [this] { if (onBringBack) onBringBack(); });
    auto *awayBox = new QVBoxLayout(m_away);
    awayBox->addStretch();
    awayBox->addWidget(awayText);
    awayBox->addSpacing(12);
    awayBox->addWidget(back, 0, Qt::AlignHCenter);
    awayBox->addStretch();

    m_controls = buildControls();
    m_mini = buildMini();
    m_corners = new CornerMask;

    m_title = new QLabel;
    m_title->setObjectName("videoTitle");
    m_title->setWordWrap(true);
    m_channel = new QLabel;
    m_channel->setObjectName("videoChannel");
    auto *popOut = action(QString(QChar(0xE8A7)), "Open in mpv's window");
    m_youtube = action(QString(QChar(0xE774)), "Watch on YouTube");
    m_popOut = popOut;
    // A song from a song list shown as its video goes back to being a song.
    m_toAudio = action(QString(QChar(0xEC4F)), "Switch to audio");
    m_toAudio->hide();
    connect(m_toAudio, &QPushButton::clicked, this, [this] { if (onSwitchToAudio) onSwitchToAudio(); });
    connect(popOut, &QPushButton::clicked, this, [this] { if (onPopOut) onPopOut(); });
    connect(m_youtube, &QPushButton::clicked, this, [this] {
        if (!m_link.isEmpty())
            QDesktopServices::openUrl(QUrl(m_link));
    });
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(m_channel, 1);
    buttons->addWidget(m_toAudio);
    buttons->addWidget(popOut);
    buttons->addWidget(m_youtube);

    // The description in a box, as YouTube has it under the player: views
    // and date in bold, a few lines, the rest with "...more" or a click on
    // the box; links, timestamps and hashtags clickable.
    m_card = new QWidget;
    m_card->setObjectName("videoDescCard");
    m_card->setAttribute(Qt::WA_StyledBackground);
    m_card->setCursor(Qt::PointingHandCursor);
    m_card->installEventFilter(this);
    m_card->hide();
    m_meta = new QLabel;
    m_meta->setObjectName("videoMeta");
    m_description = new QTextBrowser;
    m_description->setObjectName("videoDescription");
    m_description->setFrameShape(QFrame::NoFrame);
    m_description->setOpenLinks(false);
    m_description->document()->setDocumentMargin(0);
    m_description->document()->setDefaultStyleSheet(
        QStringLiteral("a { color: rgb(120,170,255); text-decoration: none; }"));
    m_description->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_description->viewport()->installEventFilter(this);
    connect(m_description, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == "seek") {
            if (onSeek)
                onSeek(url.path().toDouble());
        } else {
            QDesktopServices::openUrl(url);
        }
    });
    m_more = new QPushButton;
    m_more->setObjectName("videoMore");
    m_more->setCursor(Qt::PointingHandCursor);
    connect(m_more, &QPushButton::clicked, this, [this] { setExpanded(!m_expanded); });
    auto *card = new QVBoxLayout(m_card);
    card->setContentsMargins(14, 12, 14, 10);
    card->setSpacing(6);
    card->addWidget(m_meta);
    card->addWidget(m_description);
    card->addWidget(m_more, 0, Qt::AlignLeft);

    m_info = new QWidget;
    auto *info = new QVBoxLayout(m_info);
    info->setContentsMargins(4, 12, 4, 0);
    info->setSpacing(6);
    info->addWidget(m_title);
    info->addLayout(buttons);
    info->addSpacing(6);
    info->addWidget(m_card);

    // The whole page scrolls, as YouTube's does: the picture goes up and out
    // of view while the description and the list are read.
    m_content = new QWidget;
    m_content->setAutoFillBackground(false);
    m_page = new QScrollArea;
    m_page->setObjectName("videoInfo");
    m_page->setWidget(m_content);
    m_page->setWidgetResizable(true);
    m_page->setFrameShape(QFrame::NoFrame);
    m_page->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_page->viewport()->setAutoFillBackground(false);
    connect(m_page->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { if (onScrolled) onScrolled(); });

    m_listName = new QLabel;
    m_listName->setObjectName("sectionTitle");
    m_list = new QListWidget;
    m_list->setObjectName("songs");
    m_list->setMouseTracking(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    auto *rows = new VideoRow(m_list);
    rows->cover = [this](const QString &t) { return coverFor ? coverFor(t) : QPixmap(); };
    rows->length = [this](const QString &t) { return lengthFor ? lengthFor(t) : QString(); };
    rows->channel = [this](const QString &t) { return channelFor ? channelFor(t) : QString(); };
    // All of it shows: the page scrolls, not the list.
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setItemDelegate(rows);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (onPick)
            onPick(item->data(kTargetRole).toString());
    });
    m_side = new QWidget;
    m_side->setFixedWidth(kSideWidth);
    auto *side = new QVBoxLayout(m_side);
    side->setContentsMargins(0, 0, 0, 0);
    side->setSpacing(8);
    side->addWidget(m_listName);
    side->addWidget(m_list);
    side->addStretch();

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_page);
    m_grid = new QGridLayout(m_content);
    m_grid->setContentsMargins(24, 20, 20, 12);
    m_grid->setHorizontalSpacing(20);
    m_grid->setVerticalSpacing(0);
    m_grid->setColumnStretch(0, 1);
    m_grid->setRowStretch(1, 1);
    arrange();
}

void VideoPage::arrange()
{
    // Beside the player, the list runs down the right. In the cinema the
    // player takes the whole width and the list sits under it, beside the
    // description, as in YouTube's theatre mode.
    for (QWidget *w : {m_player, m_info, m_side})
        m_grid->removeWidget(w);
    const bool wide = m_cinema || m_full;
    m_grid->addWidget(m_player, 0, 0, 1, wide ? 2 : 1);
    m_grid->addWidget(m_info, 1, 0, Qt::AlignTop);
    if (wide)
        m_grid->addWidget(m_side, 1, 1);
    else
        m_grid->addWidget(m_side, 0, 1, 2, 1);
    m_side->layout()->setContentsMargins(0, wide ? 14 : 0, 0, 0);
}

void VideoPage::attachSurface(QWidget *host)
{
    if (!m_surface)
        m_surface = new NativeSurface(host->window());
}

void VideoPage::setSurfaceCorner(int radius)
{
    m_surface->setCorner(radius);
    static_cast<Floating *>(m_controls)->corner = radius;
    m_controls->update();
    static_cast<MiniFace *>(m_mini)->corner = radius > 0 ? radius : 8;
    auto *corners = static_cast<CornerMask *>(m_corners);
    if (corners->corner != radius) {
        corners->corner = radius;
        corners->reshape();
    }
}

QWidget *VideoPage::viewport() const
{
    return m_page->viewport();
}

QAbstractScrollArea *VideoPage::scrollArea() const
{
    return m_page;
}

void VideoPage::setSurfaceVisibleArea(const QRect &area)
{
    m_surface->setVisibleArea(area);
}

void VideoPage::placeOverPicture(const QRect &picture, const QRect &visible)
{
    auto *catcher = static_cast<CornerMask *>(m_corners);
    if (visible.isEmpty()) {
        m_corners->hide();
        return;
    }
    catcher->picture = picture.translated(-visible.topLeft());
    m_corners->setGeometry(visible);
    m_corners->update();
    m_corners->show();
    m_corners->raise();
}

void VideoPage::setOwner(QWidget *window)
{
    m_owner = window;
    for (QWidget *w : {m_controls, m_corners, m_miniWindow})
        w->setParent(window, w->windowFlags());
    auto *catcher = static_cast<CornerMask *>(m_corners);
    catcher->clicked = [this] { if (onTogglePause) onTogglePause(); };
    catcher->doubleClicked = [this] {
        // The first click of the two paused it.
        if (onTogglePause) onTogglePause();
        if (onFullScreen) onFullScreen();
    };
    catcher->wheeled = [this](QWheelEvent *event) {
        QWidget *target = m_wheelTarget ? m_wheelTarget.data() : m_page->viewport();
        QWheelEvent copy(target->mapFromGlobal(event->globalPosition()), event->globalPosition(),
                         event->pixelDelta(), event->angleDelta(), event->buttons(), event->modifiers(),
                         event->phase(), event->inverted());
        QApplication::sendEvent(target, &copy);
    };
    static_cast<MiniWindow *>(m_miniWindow)->resized = [this](const QSize &size) {
        if (onMiniResized) onMiniResized(size);
    };
    // The face over the mini player belongs to it, so it stays on top of it.
    m_mini->setParent(m_miniWindow, m_mini->windowFlags());
}

QWidget *VideoPage::buildControls()
{
    auto *bar = new ControlsBar;
    auto *previous = iconButton(icons::Icon::Previous, 32, "Previous video");
    m_play = iconButton(icons::Icon::Pause, 36, "Play / pause (Space)");
    auto *next = iconButton(icons::Icon::Next, 32, "Next video");
    auto *back10 = iconButton(icons::Icon::Back10, 32, "Back 10 seconds (Left)");
    auto *fwd10 = iconButton(icons::Icon::Forward10, 32, "Forward 10 seconds (Right)");
    connect(previous, &QAbstractButton::clicked, this, [this] { if (onPrevious) onPrevious(); });
    connect(m_play, &QAbstractButton::clicked, this, [this] { if (onTogglePause) onTogglePause(); });
    connect(next, &QAbstractButton::clicked, this, [this] { if (onNext) onNext(); });
    connect(back10, &QAbstractButton::clicked, this, [this] { if (onSkip) onSkip(-10); });
    connect(fwd10, &QAbstractButton::clicked, this, [this] { if (onSkip) onSkip(10); });

    m_mute = glyphButton(QString(QChar(0xE767)), "Mute");
    connect(m_mute, &QPushButton::clicked, this, [this] { if (onToggleMute) onToggleMute(); });
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 130);
    m_volume->setFixedWidth(84);
    m_volume->setCursor(Qt::PointingHandCursor);
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        if (m_volume->isSliderDown() || m_volume->underMouse())
            if (onVolume)
                onVolume(v);
    });

    m_seek = new QSlider(Qt::Horizontal);
    m_seek->setObjectName("seek");
    m_seek->setRange(0, 1000);
    m_seek->setCursor(Qt::PointingHandCursor);
    connect(m_seek, &QSlider::sliderPressed, this, [this] { m_dragging = true; });
    connect(m_seek, &QSlider::sliderReleased, this, [this] {
        m_dragging = false;
        if (onSeek && m_duration > 0)
            onSeek(m_duration * m_seek->value() / 1000.0);
    });
    // The wheel there moved the handle and not the video, as it once did in
    // the song bar: it scrolls the page instead.
    m_seek->installEventFilter(this);
    m_time = new QLabel(QStringLiteral("--:-- / --:--"));
    m_time->setObjectName("videoTime");

    // Subtitles, quality and speed open note menus, as YouTube's gear does.
    m_subs = glyphButton(QString(QChar(0xE7F0)), "Subtitles");
    connect(m_subs, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        bool any = false;
        bool on = false;
        for (const QVariant &v : m_tracks) {
            const QVariantMap t = v.toMap();
            if (t.value("type").toString() != "sub")
                continue;
            any = true;
            QString name = t.value("title").toString();
            const QString lang = t.value("lang").toString();
            if (name.isEmpty())
                name = lang.isEmpty() ? QStringLiteral("Track %1").arg(t.value("id").toInt()) : lang;
            else if (!lang.isEmpty())
                name += QStringLiteral(" (%1)").arg(lang);
            QAction *a = menu.addAction(name);
            a->setCheckable(true);
            a->setChecked(t.value("selected").toBool());
            on |= t.value("selected").toBool();
            const QVariant id = t.value("id");
            connect(a, &QAction::triggered, this, [this, id] { if (onSubtitle) onSubtitle(id); });
        }
        if (!any) {
            menu.addAction("This video has no subtitles")->setEnabled(false);
        } else {
            menu.addSeparator();
            QAction *off = menu.addAction("Off");
            off->setCheckable(true);
            off->setChecked(!on);
            connect(off, &QAction::triggered, this, [this] { if (onSubtitle) onSubtitle(QStringLiteral("no")); });
        }
        menu.exec(m_subs->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    });
    m_quality = textButton(QStringLiteral("Auto"), "Quality");
    connect(m_quality, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        for (int h : kHeights) {
            QAction *a = menu.addAction(h > 0 ? QStringLiteral("Up to %1p").arg(h) : QStringLiteral("Best available"));
            a->setCheckable(true);
            a->setChecked(h == m_cap);
            connect(a, &QAction::triggered, this, [this, h] { if (onQuality) onQuality(h); });
        }
        menu.exec(m_quality->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    });
    m_speed = textButton(QStringLiteral("1x"), "Speed");
    connect(m_speed, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        for (double s : kSpeeds) {
            QAction *a = menu.addAction(s == 1.0 ? QStringLiteral("Normal") : QStringLiteral("%1x").arg(s));
            a->setCheckable(true);
            a->setChecked(qFuzzyCompare(s, m_speedNow));
            connect(a, &QAction::triggered, this, [this, s] { if (onSpeed) onSpeed(s); });
        }
        menu.exec(m_speed->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    });
    // This glyph draws larger than the rest at the same size.
    auto *full = glyphButton(QString(QChar(0xE740)), "Full screen (F)", 9);
    connect(full, &QPushButton::clicked, this, [this] { if (onFullScreen) onFullScreen(); });

    auto *row = new QHBoxLayout;
    row->setSpacing(4);
    row->addWidget(previous);
    row->addWidget(m_play);
    row->addWidget(next);
    row->addSpacing(6);
    row->addWidget(back10);
    row->addWidget(fwd10);
    row->addSpacing(6);
    row->addWidget(m_mute);
    row->addWidget(m_volume);
    row->addSpacing(12);
    row->addWidget(m_time);
    row->addStretch();
    auto *miniplayer = glyphButton(QString(), "Mini player (I)");
    static_cast<FlatButton *>(miniplayer)->setPip(true);
    connect(miniplayer, &QPushButton::clicked, this, [this] { if (onMinimize) onMinimize(); });
    row->addWidget(miniplayer);
    row->addWidget(m_subs);
    row->addWidget(m_quality);
    row->addWidget(m_speed);
    row->addWidget(full);
    m_wideOnly = {back10, fwd10, m_volume, miniplayer, m_subs, m_quality, m_speed};
    auto *column = new QVBoxLayout(bar);
    column->setContentsMargins(14, 30, 14, 8);
    column->setSpacing(2);
    column->addWidget(m_seek);
    column->addLayout(row);
    return bar;
}

QWidget *VideoPage::buildMini()
{
    auto *window = new MiniWindow;
    m_miniWindow = window;
    auto *face = new MiniFace;
    face->corner = 8;
    face->carry = window;
    window->face = face;
    face->clicked = [this] { if (onRestore) onRestore(); };
    face->moved = [this](const QPoint &at) { if (onMiniMoved) onMiniMoved(at); };
    m_miniPlay = new icons::IconButton(icons::Icon::Pause, 40, "Play / pause", face);
    m_miniPlay->setRound(QColor(255, 255, 255, 230), Qt::white, QColor(16, 15, 20));
    connect(m_miniPlay, &QAbstractButton::clicked, this, [this] { if (onTogglePause) onTogglePause(); });
    auto *expand = miniButton(QString(QChar(0xE740)), "Back to the video");
    expand->setParent(face);
    connect(expand, &QPushButton::clicked, this, [this] { if (onRestore) onRestore(); });
    auto *pin = miniButton(QString(QChar(0xE718)), "Keep on top of other windows");
    pin->setParent(face);
    connect(pin, &QPushButton::clicked, this, [this, pin, window] {
        // Above every other program's windows too, as a picture in picture,
        // and still there when the app is minimised.
        m_pinned = !m_pinned;
        static_cast<FlatButton *>(pin)->setOn(m_pinned);
        pin->setToolTip(m_pinned ? "Stop keeping on top" : "Keep on top of other windows");
        window->pinned = m_pinned;
        window->applyPin(m_owner);
    });
    face->zoomed = [window](double factor) { window->zoomBy(factor); };
    auto *close = miniButton(QString(QChar(0xE8BB)), "Close (pauses the video)");
    close->setParent(face);
    connect(close, &QPushButton::clicked, this, [this] { if (onCloseMini) onCloseMini(); });
    face->buttons = {m_miniPlay, expand, pin, close};
    face->setMouseTracking(true);   // the resize cursor at its edges
    for (QWidget *b : std::as_const(face->buttons))
        b->hide();
    return face;
}

void VideoPage::setList(const QString &name, const QVector<Entry> &entries)
{
    m_listName->setText(name);
    m_list->clear();
    for (const Entry &e : entries) {
        auto *item = new QListWidgetItem(e.title);
        item->setData(kTargetRole, e.target);
        m_list->addItem(item);
    }
    m_list->setFixedHeight(qMax(1, m_list->count()) * int(84 * theme::zoom()) + 4);
}

void VideoPage::setPlaying(const QString &target, const QString &title, const QString &channel, const QString &link)
{
    if (link != m_link)
        setDescription(QString());
    m_title->setText(title);
    m_channel->setText(channel);
    m_link = link;
    m_youtube->setVisible(!link.isEmpty());
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        const bool now = item->data(kTargetRole).toString() == target;
        item->setData(kNowRole, now);

    }
}

void VideoPage::setSongVideo(bool song)
{
    m_toAudio->setVisible(song);
    m_popOut->setVisible(!song);
}

void VideoPage::setDescription(const QString &text, const QString &meta)
{
    m_meta->setText(meta);
    m_meta->setVisible(!meta.isEmpty());
    m_description->setHtml(linkify(text.trimmed()));
    m_description->setVisible(!text.trimmed().isEmpty());
    m_hasDescription = !text.trimmed().isEmpty() || !meta.isEmpty();
    m_card->setVisible(m_hasDescription);
    setExpanded(false);
}

void VideoPage::setExpanded(bool expanded)
{
    m_expanded = expanded;
    const int collapsed = m_description->fontMetrics().lineSpacing() * kCollapsedLines + 2;
    m_description->document()->setTextWidth(m_description->viewport()->width());
    const int whole = int(m_description->document()->size().height()) + 2;
    // Open, all of it shows, and the area under the player scrolls instead.
    m_description->setFixedHeight(expanded ? whole : qMin(whole, collapsed));
    m_description->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_description->verticalScrollBar()->setValue(0);

    m_more->setText(expanded ? QStringLiteral("Show less") : QStringLiteral("...more"));
    m_more->setVisible(expanded || whole > collapsed);
    m_card->setCursor(expanded ? Qt::ArrowCursor : Qt::PointingHandCursor);
    // The box's new height is only in the layout's size once it has been
    // laid out again; until then the player would keep the room it needs.
    m_info->layout()->activate();
    fitPlayer();
    QTimer::singleShot(0, this, &VideoPage::fitPlayer);
}

void VideoPage::setTimes(double position, double duration)
{
    m_duration = duration;
    m_time->setText(clock(position) + QStringLiteral(" / ") + clock(duration));
    if (!m_dragging)
        m_seek->setValue(duration > 0 && position >= 0 ? int(position / duration * 1000) : 0);
}

void VideoPage::setPaused(bool paused)
{
    m_paused = paused;
    m_play->setIcon(paused ? icons::Icon::Play : icons::Icon::Pause);
    m_miniPlay->setIcon(paused ? icons::Icon::Play : icons::Icon::Pause);
}

void VideoPage::setTracks(const QVariantList &tracks)
{
    m_tracks = tracks;
    bool on = false;
    for (const QVariant &v : tracks)
        if (v.toMap().value("type").toString() == "sub" && v.toMap().value("selected").toBool())
            on = true;
    static_cast<FlatButton *>(m_subs)->setOn(on);
}

void VideoPage::setQualityCap(int height)
{
    m_cap = height;
    m_quality->setText(height > 0 ? QStringLiteral("%1p").arg(height) : QStringLiteral("Best"));
}

void VideoPage::setSpeed(double speed)
{
    m_speedNow = speed;
    m_speed->setText(qFuzzyCompare(speed, 1.0) ? QStringLiteral("1x") : QStringLiteral("%1x").arg(speed));
}

void VideoPage::setVolume(int volume, bool muted)
{
    if (!m_volume->isSliderDown()) {
        const QSignalBlocker quiet(m_volume);
        m_volume->setValue(volume);
    }
    m_mute->setText(QString(QChar(muted || volume <= 0 ? 0xE74F : 0xE767)));
    m_mute->setToolTip(muted ? "Unmute" : "Mute");
}

void VideoPage::setAspect(double aspect)
{
    if (aspect <= 0.1 || qFuzzyCompare(aspect, m_aspect))
        return;
    m_aspect = aspect;
    static_cast<MiniWindow *>(m_miniWindow)->aspect = qBound(0.5, aspect, 2.6);
    placeScreen();
}

void VideoPage::setPoppedOut(bool out)
{
    m_popped = out;
    m_away->setVisible(out);
    m_screen->setVisible(!out);
}

void VideoPage::setFullScreen(bool full)
{
    m_full = full;
    setCinema(m_cinema);
}

void VideoPage::setCinema(bool cinema)
{
    m_cinema = cinema;
    m_info->setVisible(!m_full);
    m_side->setVisible(!m_full);
    // Full screen is the picture alone: nothing to scroll to.
    m_page->setVerticalScrollBarPolicy(m_full ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    if (m_full)
        m_page->verticalScrollBar()->setValue(0);
    arrange();
    m_grid->setContentsMargins(m_full ? QMargins() : m_cinema ? QMargins(16, 14, 16, 12) : QMargins(24, 20, 20, 12));
    fitPlayer();
}

void VideoPage::refreshCovers() const
{
    m_list->viewport()->update();
}

void VideoPage::resizeEvent(QResizeEvent *)
{
    fitPlayer();
    if (!m_expanded && m_hasDescription)
        setExpanded(false);   // "...more" only while there is more
}

void VideoPage::setControlsCompact(bool compact)
{
    for (QWidget *w : std::as_const(m_wideOnly))
        w->setVisible(!compact);
}

bool VideoPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_seek && event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        QWidget *target = m_wheelTarget ? m_wheelTarget.data() : m_page->viewport();
        QWheelEvent copy(target->mapFromGlobal(wheel->globalPosition()), wheel->globalPosition(),
                         wheel->pixelDelta(), wheel->angleDelta(), wheel->buttons(), wheel->modifiers(),
                         wheel->phase(), wheel->inverted());
        QApplication::sendEvent(target, &copy);
        return true;
    }
    if (watched == m_player && event->type() == QEvent::Resize)
        placeScreen();
    if ((watched == m_screen || watched == m_player) &&
        (event->type() == QEvent::Move || event->type() == QEvent::Resize) && onScreenMoved)
        onScreenMoved();
    // A click anywhere on the closed description opens it, as on YouTube;
    // a link in it is followed instead.
    if (!m_expanded && event->type() == QEvent::MouseButtonRelease &&
        (watched == m_card || watched == m_description->viewport())) {
        const auto *e = static_cast<QMouseEvent *>(event);
        if (e->button() == Qt::LeftButton &&
            (watched == m_card || m_description->anchorAt(e->position().toPoint()).isEmpty())) {
            setExpanded(true);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void VideoPage::placeScreen()
{
    // In full screen the picture takes it all (mpv draws its black around
    // it); elsewhere the screen is cut to the video's shape and centred.
    const QRect area = m_player->rect();
    QRect r = area;
    if (!m_full) {
        const int w = qMin(area.width(), int(area.height() * m_aspect));
        const int h = qMin(area.height(), int(w / m_aspect));
        r = QRect(area.x() + (area.width() - w) / 2, area.y() + (area.height() - h) / 2, w, h);
    }
    m_screen->setGeometry(r);
    m_away->setGeometry(area);
}

void VideoPage::fitPlayer()
{
    // The scroll bar's width is always left aside, or its coming and going
    // would change the player's size and so whether it is needed.
    const QMargins m = m_grid->contentsMargins();
    const int viewWidth = m_page->width() - (m_full ? 0 : 12);
    const int viewHeight = m_page->height();
    if (m_full) {
        m_player->setFixedHeight(qMax(180, viewHeight));
        return;
    }
    // 16:9 for the width there is, short enough that the title and the
    // start of the description show under it; the rest scrolls into view.
    if (!m_expanded)
        m_under = m_info->sizeHint().height();
    const int width = viewWidth - m.left() - m.right() - (m_cinema ? 0 : kSideWidth + 20);
    const int under = m_cinema ? 120 : m_under;
    m_player->setFixedHeight(qMax(180, qMin(width * 9 / 16, viewHeight - under - m.top() - m.bottom())));
}
