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

// The tabs in the top strip, browser-like: a small cover and the name,
// a close mark on the one under the mouse. Painted, so a dozen cost nothing.
class TabStrip : public QWidget {
public:
    explicit TabStrip(QWidget *parent = nullptr);

    QVector<VideoTab> tabs;
    int current = -1;    // the tab that is what plays; -1: none is

    std::function<QPixmap(const QString &id)> cover;
    std::function<void(int index)> onPick;
    std::function<void(int index)> onClose;
    std::function<void(int index, const QPoint &global)> onMenu;

    // Something in `tabs` changed: sized and painted again.
    void changed();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    int tabWidth() const;
    QRect tabRect(int i) const;
    QRect closeRect(int i) const;
    int tabAt(const QPoint &p) const;
    int m_hover = -1;
    bool m_hoverClose = false;
    int m_pressed = -1;
};
