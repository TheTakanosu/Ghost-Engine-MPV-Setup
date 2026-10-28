#include "videopage.h"

#include "icons.h"
#include "theme.h"
#include "tiles.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSlider>
#include <QStyledItemDelegate>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <windows.h>

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

// The window mpv draws into: a native one, so mpv can be given its handle.
// Its corners are cut round with a window region, which also clips mpv's
// window inside it; a Qt mask would not reach that far. What the cut-off
// corners uncover is painted again, or it stays black.
class Surface : public QWidget {
public:
    explicit Surface(QWidget *host) : QWidget(host)
    {
        setAttribute(Qt::WA_NativeWindow);
        setAttribute(Qt::WA_DontCreateNativeAncestors);
        // Not opaque to Qt: an opaque child's whole square is left out when
        // the window under it is painted, corners and all.
        setAutoFillBackground(false);
        winId();   // the window exists from now on
        hide();
    }

    void setCorner(int radius)
    {
        if (radius == m_corner)
            return;
        m_corner = radius;
        clip();
    }

protected:
    void resizeEvent(QResizeEvent *) override { clip(); }
    void moveEvent(QMoveEvent *event) override
    {
        if (parentWidget())
            parentWidget()->update(QRect(event->oldPos(), size()));
        clip();
    }

private:
    void clip()
    {
        const qreal dpr = devicePixelRatioF();
        const int w = int(width() * dpr), h = int(height() * dpr), r = int(2 * m_corner * dpr);
        SetWindowRgn(reinterpret_cast<HWND>(winId()),
                     r > 0 ? CreateRoundRectRgn(0, 0, w + 1, h + 1, r, r) : CreateRectRgn(0, 0, w, h), TRUE);
        if (parentWidget())
            parentWidget()->update(geometry());
    }

    int m_corner = 12;
};

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

// The mini player's face: clicked anywhere, it goes back to the video;
// with the mouse on it, it darkens and shows its buttons. Never quite
// clear, or clicks would fall through to mpv.
class MiniFace : public Floating {
public:
    std::function<void()> clicked;
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
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && clicked)
            clicked();
    }
    void resizeEvent(QResizeEvent *) override
    {
        if (buttons.size() < 3)
            return;
        buttons[0]->move((width() - buttons[0]->width()) / 2, (height() - buttons[0]->height()) / 2);
        buttons[1]->move(8, 8);
        buttons[2]->move(width() - buttons[2]->width() - 8, 8);
    }

private:
    void setHot(bool hot)
    {
        m_hot = hot;
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

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return {kSideWidth, int(84 * theme::zoom())};
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
        p->drawText(text, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, index.data(Qt::DisplayRole).toString());
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

QPushButton *glyphButton(const QString &glyph, const QString &tip)
{
    auto *b = new QPushButton(glyph);
    b->setObjectName("videoControl");
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QPushButton *textButton(const QString &text, const QString &tip)
{
    auto *b = new QPushButton(text);
    b->setObjectName("videoTextControl");
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

icons::IconButton *iconButton(icons::Icon icon, int size, const QString &tip)
{
    auto *b = new icons::IconButton(icon, size, tip);
    b->setColors(kGlyph, Qt::white);
    return b;
}

} // namespace

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

    m_title = new QLabel;
    m_title->setObjectName("videoTitle");
    m_title->setWordWrap(true);
    m_channel = new QLabel;
    m_channel->setObjectName("videoChannel");
    auto *popOut = action(QString(QChar(0xE8A7)), "Open in mpv's window");
    m_youtube = action(QString(QChar(0xE774)), "Watch on YouTube");
    connect(popOut, &QPushButton::clicked, this, [this] { if (onPopOut) onPopOut(); });
    connect(m_youtube, &QPushButton::clicked, this, [this] {
        if (!m_link.isEmpty())
            QDesktopServices::openUrl(QUrl(m_link));
    });
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addWidget(m_channel, 1);
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

    auto *left = new QVBoxLayout;
    left->setSpacing(0);
    left->addWidget(m_player);
    left->addWidget(m_info);
    left->addStretch();

    m_listName = new QLabel;
    m_listName->setObjectName("sectionTitle");
    m_list = new QListWidget;
    m_list->setObjectName("songs");
    m_list->setMouseTracking(true);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    auto *rows = new VideoRow(m_list);
    rows->cover = [this](const QString &t) { return coverFor ? coverFor(t) : QPixmap(); };
    rows->length = [this](const QString &t) { return lengthFor ? lengthFor(t) : QString(); };
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
    side->addWidget(m_list, 1);

    auto *box = new QHBoxLayout(this);
    box->setContentsMargins(24, 20, 20, 12);
    box->setSpacing(20);
    box->addLayout(left, 1);
    box->addWidget(m_side);
}

void VideoPage::attachSurface(QWidget *host)
{
    if (!m_surface)
        m_surface = new Surface(host);
}

void VideoPage::setSurfaceCorner(int radius)
{
    static_cast<Surface *>(m_surface)->setCorner(radius);
    for (QWidget *w : {m_controls, m_mini}) {
        static_cast<Floating *>(w)->corner = radius;
        w->update();
    }
}

void VideoPage::setOwner(QWidget *window)
{
    for (QWidget *w : {m_controls, m_mini})
        w->setParent(window, w->windowFlags());
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
    m_time = new QLabel(QStringLiteral("--:-- / --:--"));
    m_time->setObjectName("videoTime");

    // Subtitles, quality and speed open small menus, as YouTube's gear does.
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
    auto *full = glyphButton(QString(QChar(0xE740)), "Full screen (F)");
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
    row->addWidget(m_subs);
    row->addWidget(m_quality);
    row->addWidget(m_speed);
    row->addWidget(full);
    auto *column = new QVBoxLayout(bar);
    column->setContentsMargins(14, 30, 14, 8);
    column->setSpacing(2);
    column->addWidget(m_seek);
    column->addLayout(row);
    return bar;
}

QWidget *VideoPage::buildMini()
{
    auto *face = new MiniFace;
    face->corner = 10;
    face->clicked = [this] { if (onRestore) onRestore(); };
    m_miniPlay = new icons::IconButton(icons::Icon::Pause, 48, "Play / pause", face);
    m_miniPlay->setRound(QColor(255, 255, 255, 230), Qt::white, QColor(16, 15, 20));
    connect(m_miniPlay, &QAbstractButton::clicked, this, [this] { if (onTogglePause) onTogglePause(); });
    auto *expand = glyphButton(QString(QChar(0xE740)), "Back to the video");
    expand->setParent(face);
    connect(expand, &QPushButton::clicked, this, [this] { if (onRestore) onRestore(); });
    auto *close = glyphButton(QString(QChar(0xE8BB)), "Close (pauses the video)");
    close->setParent(face);
    connect(close, &QPushButton::clicked, this, [this] { if (onCloseMini) onCloseMini(); });
    face->buttons = {m_miniPlay, expand, close};
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
        if (now)
            m_list->scrollToItem(item, QAbstractItemView::EnsureVisible);
    }
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
    m_description->setFixedHeight(expanded ? qMin(whole, 300) : qMin(whole, collapsed));
    m_description->setVerticalScrollBarPolicy(expanded ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
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
    m_subs->setStyleSheet(on ? QStringLiteral("color: rgb(176,127,224);") : QString());
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
    const bool bare = m_full || m_cinema;
    m_info->setVisible(!bare);
    m_side->setVisible(!bare);
    layout()->setContentsMargins(m_full ? QMargins() : m_cinema ? QMargins(16, 12, 16, 12) : QMargins(24, 20, 20, 12));
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

bool VideoPage::eventFilter(QObject *watched, QEvent *event)
{
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
    // Full screen and cinema: the page's whole height. Otherwise 16:9 for
    // the width there is, short enough to leave the title in view.
    const QMargins m = layout()->contentsMargins();
    if (m_full || m_cinema) {
        m_player->setFixedHeight(qMax(180, height() - m.top() - m.bottom()));
        return;
    }
    const int width = this->width() - kSideWidth - m.left() - m.right() - 20;
    const int under = m_info->sizeHint().height();
    m_player->setFixedHeight(qMax(180, qMin(width * 9 / 16, height() - under - m.top() - m.bottom())));
}
