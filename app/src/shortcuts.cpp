#include "shortcuts.h"

#include "theme.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QSettings>
#include <QToolTip>
#include <QVBoxLayout>

namespace shortcuts {

namespace {

bool g_capturing = false;

QString settingFor(const QString &id) { return QStringLiteral("keys/") + id; }

} // namespace

const QVector<Action> &actions()
{
    // Spotify's keys where it has one, so most hands already know them.
    static const QVector<Action> table = {
        {"playPause", "Play / pause", "Playback", "Space"},
        {"next", "Next", "Playback", "Ctrl+Right"},
        {"previous", "Previous", "Playback", "Ctrl+Left"},
        {"forward", "Seek forward", "Playback", "Right", false, true},
        {"back", "Seek back", "Playback", "Left", false, true},
        {"volumeUp", "Volume up", "Playback", "Ctrl+Up", false, true},
        {"volumeDown", "Volume down", "Playback", "Ctrl+Down", false, true},
        {"mute", "Mute", "Playback", "M"},
        {"shuffle", "Shuffle", "Playback", "Ctrl+S"},
        {"repeat", "Repeat", "Playback", "Ctrl+R"},
        {"fullScreen", "Full screen", "Video", "F"},
        {"mini", "Mini player", "Video", "I"},
        {"subtitles", "Subtitles on / off", "Video", "C"},
        {"search", "Search", "Go to", "Ctrl+K", true},
        {"librarySearch", "Search in Your Library", "Go to", "Ctrl+Shift+F", true},
        {"home", "Home", "Go to", "Alt+H"},
        {"lyrics", "Lyrics", "Go to", "Alt+L"},
        {"settings", "Settings", "Go to", "Ctrl+,"},
        {"shortcuts", "Keyboard shortcuts", "Go to", "Ctrl+/", true},
    };
    return table;
}

const Action *find(const QString &id)
{
    for (const Action &a : actions())
        if (a.id == id)
            return &a;
    return nullptr;
}

QString keyFor(const QString &id)
{
    const Action *a = find(id);
    if (!a)
        return {};
    const QVariant saved = QSettings().value(settingFor(id));
    return saved.isValid() ? saved.toString() : a->defaultKey;
}

QString setKey(const QString &id, const QString &key)
{
    QString taken;
    if (!key.isEmpty())
        for (const Action &a : actions())
            if (a.id != id && keyFor(a.id) == key) {
                QSettings().setValue(settingFor(a.id), QString());
                taken = a.name;
            }
    const Action *a = find(id);
    if (a && key == a->defaultKey)
        QSettings().remove(settingFor(id));
    else
        QSettings().setValue(settingFor(id), key);
    return taken;
}

void resetAll()
{
    for (const Action &a : actions())
        QSettings().remove(settingFor(a.id));
}

QString keyOf(const QKeyEvent *event)
{
    const int key = event->key();
    if (key == Qt::Key_unknown || key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
        key == Qt::Key_Meta || key == Qt::Key_AltGr)
        return {};
    // Keypad keys count as the same key on the main block.
    const Qt::KeyboardModifiers mods = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    int k = key;
    // Shift with a number row key gives the shifted character on some
    // layouts; the key is named by what it is without Shift.
    if (key == Qt::Key_Backtab)
        k = Qt::Key_Tab;
    // A sign that needs Shift on this keyboard ("/" is Shift+7 on Turkish Q)
    // is named by the sign alone: Ctrl+/ is Ctrl+/ on every layout.
    Qt::KeyboardModifiers m = mods;
    const bool sign = (k >= 0x21 && k <= 0x2F) || (k >= 0x3A && k <= 0x40) || (k >= 0x5B && k <= 0x60) || (k >= 0x7B && k <= 0x7E);
    if (sign)
        m &= ~Qt::ShiftModifier;
    return QKeySequence(QKeyCombination(m, Qt::Key(k))).toString(QKeySequence::PortableText);
}

QString keyOfMouse(Qt::MouseButton button)
{
    if (button == Qt::BackButton)
        return QStringLiteral("Mouse 4");
    if (button == Qt::ForwardButton)
        return QStringLiteral("Mouse 5");
    return {};
}

QString actionFor(const QString &key)
{
    if (key.isEmpty())
        return {};
    for (const Action &a : actions())
        if (keyFor(a.id) == key)
            return a.id;
    return {};
}

QString display(const QString &key)
{
    if (key.isEmpty())
        return QStringLiteral("None");
    if (key.startsWith(QLatin1String("Mouse")))
        return key;
    QString text = QKeySequence::fromString(key, QKeySequence::PortableText).toString(QKeySequence::NativeText);
    if (text.isEmpty())
        text = key;
    // The arrows as arrows; "+" between keys with room around it.
    text.replace(QLatin1String("Right"), QStringLiteral("→")).replace(QLatin1String("Left"), QStringLiteral("←"))
        .replace(QLatin1String("Up"), QStringLiteral("↑")).replace(QLatin1String("Down"), QStringLiteral("↓"));
    QStringList parts;
    QString rest = text;
    while (true) {
        const int plus = rest.indexOf('+', 1);
        if (plus < 0)
            break;
        parts << rest.left(plus);
        rest = rest.mid(plus + 1);
    }
    parts << rest;
    return parts.join(QStringLiteral(" + "));
}

bool capturing()
{
    return g_capturing;
}

// The button a key is picked with: click it, press the key (or a mouse
// side button). Esc leaves it as it was, Backspace leaves it with none.
class KeyButton : public QPushButton {
public:
    explicit KeyButton(const QString &id) : m_id(id)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumWidth(170);
        theme::style(this, QStringLiteral("QPushButton { background: @card; border: 1px solid @line; border-radius: 8px;"
                                          " padding: 5px 14px; font-weight: 600; }"
                                          "QPushButton:hover { border-color: @muted; }"
                                          "QPushButton:checked { border-color: @accent; color: @accentText; }"));
        setCheckable(true);
        connect(this, &QPushButton::clicked, this, [this](bool on) { on ? start() : stop(); });
    }

    std::function<void(const QString &)> onPicked;

    void showKey() { setText(display(keyFor(m_id))); }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (!m_picking) {
            QPushButton::keyPressEvent(event);
            return;
        }
        if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier) {
            stop();
            return;
        }
        if ((event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) && event->modifiers() == Qt::NoModifier) {
            pick(QString());
            return;
        }
        const QString key = keyOf(event);
        if (!key.isEmpty())
            pick(key);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_picking) {
            const QString key = keyOfMouse(event->button());
            if (!key.isEmpty()) {
                pick(key);
                return;
            }
            if (!rect().contains(event->position().toPoint())) {
                stop();   // a click elsewhere leaves it as it was
                return;
            }
        }
        QPushButton::mousePressEvent(event);
    }
    void focusOutEvent(QFocusEvent *event) override
    {
        if (m_picking)
            stop();
        QPushButton::focusOutEvent(event);
    }

private:
    void start()
    {
        m_picking = true;
        g_capturing = true;
        setChecked(true);
        setText(QStringLiteral("Press a key..."));
        grabKeyboard();
        grabMouse();
    }
    void stop()
    {
        if (!m_picking)
            return;
        m_picking = false;
        g_capturing = false;
        releaseKeyboard();
        releaseMouse();
        setChecked(false);
        showKey();
    }
    void pick(const QString &key)
    {
        stop();
        if (onPicked)
            onPicked(key);
    }

    QString m_id;
    bool m_picking = false;
};

// A full keyboard (numpad too: most people have one) and a mouse, drawn,
// with each key that does something lit in the accent and named under the
// mouse pointer, and the key being pressed right now shown pressed.
class KeyboardMap : public QWidget {
public:
    KeyboardMap()
    {
        setMouseTracking(true);
        setMinimumHeight(260);
        build();
    }

    QSize sizeHint() const override { return {1000, 290}; }

protected:
    void showEvent(QShowEvent *) override { qApp->installEventFilter(this); }
    void hideEvent(QHideEvent *) override
    {
        qApp->removeEventFilter(this);
        m_down.clear();
    }

    // Every key and side button the app sees while the page shows, only
    // looked at: what it does still happens.
    bool eventFilter(QObject *, QEvent *event) override
    {
        const QEvent::Type t = event->type();
        if (t == QEvent::KeyPress || t == QEvent::KeyRelease) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->isAutoRepeat())
                return false;
            const QString name = nameOf(key);
            if (!name.isEmpty() && (t == QEvent::KeyPress ? !m_down.contains(name) : m_down.contains(name))) {
                if (t == QEvent::KeyPress)
                    m_down.insert(name);
                else
                    m_down.remove(name);
                update();
            }
        } else if (t == QEvent::MouseButtonPress || t == QEvent::MouseButtonRelease) {
            const QString name = keyOfMouse(static_cast<QMouseEvent *>(event)->button());
            if (!name.isEmpty()) {
                if (t == QEvent::MouseButtonPress)
                    m_down.insert(name);
                else
                    m_down.remove(name);
                update();
            }
        } else if (t == QEvent::ApplicationDeactivate && !m_down.isEmpty()) {
            m_down.clear();
            update();
        }
        return false;
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QHash<QString, QStringList> used = usedKeys();
        const qreal unit = layoutUnit();
        const QPointF origin = mapOrigin(unit);

        // The mouse's body under its buttons.
        const QRectF body = rectFor({QString(), QString(), kMouseX, 1.25, 2.4, 5.0}, unit, origin);
        QPainterPath shell;
        shell.addRoundedRect(body, body.width() / 2, body.width() / 2);
        p.fillPath(shell, theme::card());
        p.setPen(QPen(theme::c().line, 1.2));
        p.drawPath(shell);
        const qreal split = body.top() + body.height() * 0.38;
        p.drawLine(QPointF(body.left() + 2, split), QPointF(body.right() - 2, split));
        p.drawLine(QPointF(body.center().x(), body.top()), QPointF(body.center().x(), split));
        const QRectF wheel(body.center().x() - unit * 0.13, body.top() + body.height() * 0.1, unit * 0.26, body.height() * 0.17);
        p.setBrush(theme::c().line);
        p.drawRoundedRect(wheel, unit * 0.13, unit * 0.13);
        p.setBrush(Qt::NoBrush);

        for (const Key &k : m_keys) {
            const QRectF r = rectFor(k, unit, origin);
            const QStringList what = used.value(boundName(k.name));
            const bool lit = !what.isEmpty();
            const bool down = m_down.contains(k.name);
            const bool modifier = k.name == "Ctrl" || k.name == "Shift" || k.name == "Alt";
            QColor fill = theme::card();
            if (lit)
                fill = theme::accent();
            if (k.name == m_hover && !lit)
                fill = theme::c().raised;
            if (down)
                fill = lit ? theme::c().accentHover : theme::c().raisedHover;
            // Pressed: a little smaller and lower, as a real key goes down.
            const qreal in = down ? 3.5 : 1.5;
            const qreal drop = down ? 1.5 : 0;
            QPainterPath path;
            path.addRoundedRect(r.adjusted(in, in + drop, -in, -in + drop), 5, 5);
            p.fillPath(path, fill);
            if (down) {
                p.setPen(QPen(theme::c().text, 1.8));
                p.drawPath(path);
            } else if (modifier && m_modifiersUsed.contains(k.name)) {
                // Ctrl, Shift and Alt held with some shortcut: ringed in the accent.
                p.setPen(QPen(theme::accent(), 1.6));
                p.drawPath(path);
            }
            QFont f = font();
            f.setPixelSize(int(qMax(9.0, unit * 0.26)));
            f.setWeight(lit || down ? QFont::Bold : QFont::Medium);
            p.setFont(f);
            p.setPen(down ? theme::c().text : lit ? theme::c().onAccent : theme::muted());
            p.drawText(r.translated(0, drop), Qt::AlignCenter, k.label);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const qreal unit = layoutUnit();
        const QPointF origin = mapOrigin(unit);
        QString over;
        for (const Key &k : m_keys)
            if (rectFor(k, unit, origin).contains(event->position()))
                over = k.name;
        if (over != m_hover) {
            m_hover = over;
            update();
        }
        const QStringList what = usedKeys().value(boundName(over));
        if (what.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(event->globalPosition().toPoint(), what.join('\n'), this);
    }

    void leaveEvent(QEvent *) override
    {
        m_hover.clear();
        update();
    }

private:
    struct Key {
        QString name;    // as in a key's portable text ("Space", "Right", "Mouse 4"); "kp:7" on the numpad
        QString label;   // as printed on it
        qreal x, y, w, h;
    };

    static constexpr qreal kNumpadX = 19.0;
    static constexpr qreal kMouseX = 24.0;
    static constexpr qreal kWidth = 26.4;

    // A numpad key does what the same key on the main block does.
    static QString boundName(const QString &name)
    {
        return name.startsWith(QLatin1String("kp:")) ? name.mid(3) : name;
    }

    // The drawn key a press is: the numpad's own when it came from there.
    static QString nameOf(const QKeyEvent *event)
    {
        switch (event->key()) {
        case Qt::Key_Control: return QStringLiteral("Ctrl");
        case Qt::Key_Shift: return QStringLiteral("Shift");
        case Qt::Key_Alt: case Qt::Key_AltGr: return QStringLiteral("Alt");
        case Qt::Key_Meta: return QStringLiteral("Meta");
        default: break;
        }
        int k = event->key();
        if (k == Qt::Key_Backtab)
            k = Qt::Key_Tab;
        const QString name = QKeySequence(k).toString(QKeySequence::PortableText);
        if (name.isEmpty())
            return {};
        return (event->modifiers() & Qt::KeypadModifier) ? QStringLiteral("kp:") + name : name;
    }

    void add(qreal &x, qreal y, const QString &name, const QString &label, qreal w = 1, qreal h = 1)
    {
        m_keys.push_back({name, label, x, y, w, h});
        x += w;
    }

    void build()
    {
        qreal x = 0;
        add(x, 0, "Esc", "Esc");
        x += 0.5;
        for (int i = 1; i <= 12; ++i) {
            add(x, 0, QStringLiteral("F%1").arg(i), QStringLiteral("F%1").arg(i));
            if (i % 4 == 0)
                x += 0.25;
        }
        x = 0;
        for (const char *k : {"`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "="})
            add(x, 1.25, QString::fromLatin1(k), QString::fromLatin1(k));
        add(x, 1.25, "Backspace", "⌫", 2);
        x = 0;
        add(x, 2.25, "Tab", "Tab", 1.5);
        for (const char *k : {"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]"})
            add(x, 2.25, QString::fromLatin1(k), QString::fromLatin1(k));
        add(x, 2.25, "\\", "\\", 1.5);
        x = 0;
        add(x, 3.25, "CapsLock", "Caps", 1.75);
        for (const char *k : {"A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'"})
            add(x, 3.25, QString::fromLatin1(k), QString::fromLatin1(k));
        add(x, 3.25, "Return", "Enter", 2.25);
        x = 0;
        add(x, 4.25, "Shift", "Shift", 2.25);
        for (const char *k : {"Z", "X", "C", "V", "B", "N", "M", ",", ".", "/"})
            add(x, 4.25, QString::fromLatin1(k), QString::fromLatin1(k));
        add(x, 4.25, "Shift", "Shift", 2.75);
        x = 0;
        add(x, 5.25, "Ctrl", "Ctrl", 1.25);
        add(x, 5.25, "Meta", "Win", 1.25);
        add(x, 5.25, "Alt", "Alt", 1.25);
        add(x, 5.25, "Space", "Space", 6.25);
        add(x, 5.25, "Alt", "Alt", 1.25);
        add(x, 5.25, "Meta", "Win", 1.25);
        add(x, 5.25, "Menu", "☰", 1.25);
        add(x, 5.25, "Ctrl", "Ctrl", 1.25);
        // The block beside it.
        const qreal nav = 15.5;
        x = nav; add(x, 0, "Print", "PrtSc"); add(x, 0, "ScrollLock", "ScrLk"); add(x, 0, "Pause", "Pause");
        x = nav; add(x, 1.25, "Ins", "Ins"); add(x, 1.25, "Home", "Home"); add(x, 1.25, "PgUp", "PgUp");
        x = nav; add(x, 2.25, "Del", "Del"); add(x, 2.25, "End", "End"); add(x, 2.25, "PgDown", "PgDn");
        x = nav + 1; add(x, 4.25, "Up", "↑");
        x = nav; add(x, 5.25, "Left", "←"); add(x, 5.25, "Down", "↓"); add(x, 5.25, "Right", "→");
        // The numpad.
        const qreal np = kNumpadX;
        x = np; add(x, 1.25, "kp:NumLock", "Num"); add(x, 1.25, "kp:/", "/"); add(x, 1.25, "kp:*", "*"); add(x, 1.25, "kp:-", "-");
        x = np; add(x, 2.25, "kp:7", "7"); add(x, 2.25, "kp:8", "8"); add(x, 2.25, "kp:9", "9"); add(x, 2.25, "kp:+", "+", 1, 2);
        x = np; add(x, 3.25, "kp:4", "4"); add(x, 3.25, "kp:5", "5"); add(x, 3.25, "kp:6", "6");
        x = np; add(x, 4.25, "kp:1", "1"); add(x, 4.25, "kp:2", "2"); add(x, 4.25, "kp:3", "3"); add(x, 4.25, "kp:Enter", "Ent", 1, 2);
        x = np; add(x, 5.25, "kp:0", "0", 2); add(x, 5.25, "kp:.", ".");
        // A mouse: the two side buttons on its left flank can be given things to do.
        x = kMouseX - 0.2; add(x, 3.0, "Mouse 5", "M5", 0.7, 0.8);
        x = kMouseX - 0.2; add(x, 3.9, "Mouse 4", "M4", 0.7, 0.8);
    }

    qreal layoutUnit() const { return qMin(width() / (kWidth + 0.4), height() / 6.4); }
    QPointF mapOrigin(qreal unit) const { return {(width() - kWidth * unit) / 2, (height() - 6.25 * unit) / 2}; }
    QRectF rectFor(const Key &k, qreal unit, const QPointF &origin) const
    {
        return {origin.x() + k.x * unit, origin.y() + k.y * unit, k.w * unit, k.h * unit};
    }

    // Each key's name → what it does, with the modifiers held for it.
    QHash<QString, QStringList> usedKeys() const
    {
        QHash<QString, QStringList> used;
        m_modifiersUsed.clear();
        for (const Action &a : shortcuts::actions()) {
            const QString key = keyFor(a.id);
            if (key.isEmpty())
                continue;
            QString base = key;
            QStringList mods;
            if (!key.startsWith(QLatin1String("Mouse"))) {
                // "Ctrl++" ends in the "+" key itself.
                const int cut = key.endsWith(QLatin1String("++")) ? key.size() - 2 : key.lastIndexOf('+');
                if (cut > 0) {
                    base = key.mid(cut + 1);
                    mods = key.left(cut).split('+');
                }
            }
            for (const QString &m : mods)
                m_modifiersUsed.insert(m);
            used[base] << (mods.isEmpty() ? a.name : QStringLiteral("%1 + %2: %3").arg(mods.join(QStringLiteral(" + ")), base, a.name));
        }
        return used;
    }

    QVector<Key> m_keys;
    QString m_hover;
    QSet<QString> m_down;
    mutable QSet<QString> m_modifiersUsed;
};

} // namespace shortcuts

ShortcutsPage::ShortcutsPage(QWidget *parent) : QWidget(parent)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(28, 22, 28, 28);
    column->setSpacing(14);
    auto *title = new QLabel("Keyboard shortcuts");
    title->setObjectName("pageTitle");
    auto *about = new QLabel("Click a key on the right of an action, then press the key you want. A mouse's side buttons "
                             "work too. Esc leaves it as it was, Backspace leaves it with none. Lit keys do something; "
                             "point at one to see what.");
    about->setObjectName("statLabel");
    about->setWordWrap(true);
    column->addWidget(title);
    column->addWidget(about);
    m_map = new shortcuts::KeyboardMap;
    column->addWidget(m_map);

    QString group;
    QVBoxLayout *rows = nullptr;
    for (const shortcuts::Action &a : shortcuts::actions()) {
        if (a.group != group) {
            group = a.group;
            auto *card = new QWidget;
            card->setObjectName("statCard");
            card->setAttribute(Qt::WA_StyledBackground);
            rows = new QVBoxLayout(card);
            rows->setContentsMargins(18, 12, 18, 12);
            rows->setSpacing(4);
            auto *head = new QLabel(group);
            head->setObjectName("sectionTitle");
            rows->addWidget(head);
            column->addWidget(card);
        }
        auto *row = new QHBoxLayout;
        auto *name = new QLabel(a.name);
        name->setObjectName("settingName");
        auto *button = new shortcuts::KeyButton(a.id);
        button->showKey();
        const QString id = a.id;
        button->onPicked = [this, id](const QString &key) {
            const QString taken = shortcuts::setKey(id, key);
            if (!taken.isEmpty() && onToast)
                onToast(QStringLiteral("%1 was %2's key; %2 has none now").arg(shortcuts::display(key), taken));
            refresh();
        };
        auto *reset = new QPushButton(QString(QChar(0xE777)));   // undo arrow
        reset->setObjectName("glyph");
        reset->setCursor(Qt::PointingHandCursor);
        reset->setToolTip(QStringLiteral("Back to %1").arg(shortcuts::display(a.defaultKey)));
        const QString defaultKey = a.defaultKey;
        connect(reset, &QPushButton::clicked, this, [this, id, defaultKey] {
            const QString taken = shortcuts::setKey(id, defaultKey);
            if (!taken.isEmpty() && onToast)
                onToast(QStringLiteral("%1 was %2's key; %2 has none now").arg(shortcuts::display(defaultKey), taken));
            refresh();
        });
        row->addWidget(name, 1);
        row->addWidget(button);
        row->addWidget(reset);
        rows->addLayout(row);
        m_buttons << button;
    }
    auto *all = new QPushButton("Reset all to default");
    all->setObjectName("link");
    all->setCursor(Qt::PointingHandCursor);
    connect(all, &QPushButton::clicked, this, [this] {
        shortcuts::resetAll();
        refresh();
        if (onToast)
            onToast("Every shortcut is back to its default");
    });
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(all);
    bottom->addStretch();
    column->addLayout(bottom);
    column->addStretch(1);
}

void ShortcutsPage::refresh()
{
    for (QPushButton *b : m_buttons)
        static_cast<shortcuts::KeyButton *>(b)->showKey();
    m_map->update();
}
