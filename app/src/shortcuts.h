#pragma once

#include <QPushButton>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QKeyEvent;
class QVBoxLayout;

// The app's keyboard (and mouse side button) shortcuts: one table of what
// can be done, each with a key Batın or anyone can change on the
// Keyboard shortcuts page. A key is kept as Qt's portable text ("Ctrl+Right",
// "Space", "9") or "Mouse 4" / "Mouse 5"; an empty key means none.
// All of them are handled in one place (MainWindow's event filter), since
// mpv inside the app takes no keys of its own.
namespace shortcuts {

struct Action {
    QString id;
    QString name;
    QString group;
    QString defaultKey;
    bool whileTyping = false;   // also with the cursor in a text box
    bool repeats = false;       // a held key does it again (seek, volume)
};

const QVector<Action> &actions();
const Action *find(const QString &id);

QString keyFor(const QString &id);
// Gives `key` to `id`; another action that had it is left with none, and
// its name is returned so the page can say so.
QString setKey(const QString &id, const QString &key);
void resetAll();

// The action a key press or a mouse side button is bound to, or empty.
QString keyOf(const QKeyEvent *event);
QString keyOfMouse(Qt::MouseButton button);
QString actionFor(const QString &key);

// "Ctrl + →" for "Ctrl+Right".
QString display(const QString &key);

// While a key is being picked on the page, nothing else takes the keys.
bool capturing();

} // namespace shortcuts

// The page: a keyboard and a mouse drawn with the keys in use lit, and a
// row per action to set its key.
class ShortcutsPage : public QWidget {
    Q_OBJECT
public:
    explicit ShortcutsPage(QWidget *parent = nullptr);
    void refresh();

    std::function<void(const QString &)> onToast;

private:
    QWidget *m_map = nullptr;
    QVector<QPushButton *> m_buttons;
};
