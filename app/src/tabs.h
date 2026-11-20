#pragma once

#include <QPixmap>
#include <QVector>
#include <QWidget>

#include <functional>

// A video kept for later in a tab (Batın, 2026-11-20): only its name, its
// cover and where it was left. Nothing of it is loaded until it is clicked;
// the one mpv then opens it at that second.
struct VideoTab {
    QString target;    // the list entry mpv opens
    QString id;        // YouTube's, for the cover
    QString title;
    QString channel;
    QString list;      // the list it was in, when it came from one
    double seconds = 0;
};

// The tabs, painted, so a dozen cost nothing. In the top strip
// (horizontal): browser-like, as many as fit, the rest behind a "+N" chip
// that opens all of them as a list (Batın, 2026-11-20: a full strip pressed
// the island and ran into it). In that list (vertical): a row each, the
// cover, the name and the channel.
class TabStrip : public QWidget {
public:
    explicit TabStrip(bool vertical = false, QWidget *parent = nullptr);

    QVector<VideoTab> tabs;
    int current = -1;    // the tab that is what plays; -1: none is
    int viewing = -1;    // the tab whose page shows without playing (Batin: what plays goes on)
    // Collapsed Tabs (Settings): every tab behind one button, opened by a
    // click or by resting the mouse on it.
    bool collapsed = false;

    std::function<QPixmap(const QString &id)> cover;
    std::function<void(int index)> onPick;
    std::function<void(int index)> onClose;
    std::function<void(int index, const QPoint &global)> onMenu;
    std::function<void(const QPoint &below)> onMore;   // the "+N" chip: all of them

    // Something in `tabs` changed: sized and painted again.
    void changed();
    // The narrowest the strip can be: one tab and the chip.
    int smallest() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void lay();                      // which tabs show where, and the chip
    QRect closeRect(const QRect &tab) const;
    int tabAt(const QPoint &p) const;   // an index into `tabs`; -1: none
    bool m_vertical;
    QVector<QPair<int, QRect>> m_shown;   // the tabs that fit, with their places
    QRect m_more;                        // the "+N" chip; empty when all fit
    int m_hover = -1;
    bool m_hoverClose = false;
    bool m_hoverMore = false;
    int m_pressed = -1;
    bool m_pressedMore = false;
    class QTimer *m_hoverOpen = nullptr;
};
