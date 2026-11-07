#pragma once

#include <QNetworkAccessManager>
#include <QTimer>
#include <QWidget>

#include <functional>

class QLineEdit;
class QListWidget;

// The search box in the middle of the window, as a browser's address bar
// opens over the page (Settings > Appearance, or Ctrl+K anywhere): what was
// searched before and what YouTube suggests as it is typed. A window of its
// own over the app, so it stays above a playing video too, which is a window
// of mpv's.
class SearchPopup : public QWidget {
public:
    explicit SearchPopup(QWidget *owner);

    // Over the owner, the box holding `text`, selected.
    void open(const QString &text, const QString &placeholder);
    std::function<void(const QString &)> onSearch;

    // What was searched, newest first (search/recent, at most 12).
    static void remember(const QString &query);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool event(QEvent *event) override;

private:
    void refresh();
    void run(const QString &text);
    void layoutCard();

    QWidget *m_owner;
    QWidget *m_card;
    QLineEdit *m_edit;
    QListWidget *m_list;
    QNetworkAccessManager m_net;
    QTimer m_soon;
    int m_serial = 0;
};
