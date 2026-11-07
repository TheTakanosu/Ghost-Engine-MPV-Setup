#include "searchpopup.h"

#include "theme.h"

#include <QApplication>
#include <QFrame>
#include <QSet>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkReply>
#include <QPainter>
#include <QSettings>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {

constexpr int kRecentMax = 12;
constexpr int kRowKind = Qt::UserRole + 1;   // "recent" or "suggested"

QStringList recent()
{
    return QSettings().value("search/recent").toStringList();
}

} // namespace

SearchPopup::SearchPopup(QWidget *owner)
    : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint), m_owner(owner)
{
    setAttribute(Qt::WA_TranslucentBackground);
    m_card = new QFrame(this);
    m_card->setObjectName("searchPopup");
    theme::style(m_card, QStringLiteral("QFrame#searchPopup { background: @popup; border: 1px solid @lineStrong; border-radius: 16px; }"));
    m_edit = new QLineEdit;
    m_edit->setClearButtonEnabled(true);
    theme::style(m_edit, QStringLiteral("QLineEdit { background: transparent; border: none; color: @textBright;"
                                        " font-size: 13pt; padding: 10px 6px; selection-background-color: @accent; }"));
    m_list = new QListWidget;
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setFocusPolicy(Qt::NoFocus);
    theme::style(m_list, QStringLiteral(
        "QListWidget { background: transparent; }"
        "QListWidget::item { min-height: 40px; padding-left: 10px; border-radius: 10px; color: @text; }"
        "QListWidget::item:hover { background: @raised; }"
        "QListWidget::item:selected { background: @raisedHover; color: @textBright; }"));
    QFont rows = m_list->font();
    rows.setPointSizeF(10.5);
    rows.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Segoe Fluent Icons"), QStringLiteral("Segoe MDL2 Assets")});
    m_list->setFont(rows);
    auto *line = new QFrame;
    line->setFixedHeight(1);
    theme::style(line, QStringLiteral("background: @line;"));
    auto *box = new QVBoxLayout(m_card);
    box->setContentsMargins(14, 6, 14, 12);
    box->setSpacing(8);
    box->addWidget(m_edit);
    box->addWidget(line);
    box->addWidget(m_list);

    m_soon.setSingleShot(true);
    m_soon.setInterval(160);
    connect(&m_soon, &QTimer::timeout, this, &SearchPopup::refresh);
    connect(m_edit, &QLineEdit::textEdited, this, [this] { m_soon.start(); });
    connect(m_edit, &QLineEdit::returnPressed, this, [this] {
        QListWidgetItem *picked = m_list->currentItem();
        run(picked && picked->isSelected() ? picked->data(Qt::UserRole).toString() : m_edit->text());
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { run(item->data(Qt::UserRole).toString()); });
    m_edit->installEventFilter(this);
}

void SearchPopup::remember(const QString &query)
{
    const QString q = query.trimmed();
    if (q.isEmpty())
        return;
    QStringList list = recent();
    list.removeAll(q);
    list.prepend(q);
    QSettings().setValue("search/recent", list.mid(0, kRecentMax));
}

void SearchPopup::open(const QString &text, const QString &placeholder)
{
    setGeometry(m_owner->geometry());
    m_edit->setPlaceholderText(placeholder);
    m_edit->setText(text);
    m_edit->selectAll();
    refresh();
    show();
    raise();
    activateWindow();
    m_edit->setFocus();
}

void SearchPopup::layoutCard()
{
    // A third of the way down, as wide as a page's column reads well.
    const int w = qMin(qRound(680 * theme::zoom()), width() - 80);
    const int rows = qMin(m_list->count(), 8);
    m_list->setFixedHeight(rows ? rows * qRound(42 * theme::zoom()) + 4 : 0);
    m_list->setVisible(rows > 0);
    m_card->adjustSize();
    m_card->setGeometry((width() - w) / 2, height() / 4, w, m_card->sizeHint().height());
}

void SearchPopup::refresh()
{
    const QString typed = m_edit->text().trimmed();
    m_list->clear();
    // What was searched before, those that begin with what is typed first.
    int shown = 0;
    for (const QString &q : recent()) {
        if (!typed.isEmpty() && !q.startsWith(typed, Qt::CaseInsensitive))
            continue;
        auto *row = new QListWidgetItem(QString(QChar(0xE81C)) + QStringLiteral("    ") + q);
        row->setData(Qt::UserRole, q);
        row->setData(kRowKind, QStringLiteral("recent"));
        m_list->addItem(row);
        if (++shown == (typed.isEmpty() ? 6 : 3))
            break;
    }
    layoutCard();
    if (typed.isEmpty())
        return;
    // Then YouTube's own suggestions for it, as its search box gives them.
    const int serial = ++m_serial;
    QUrl url(QStringLiteral("https://suggestqueries.google.com/complete/search"));
    QUrlQuery query;
    query.addQueryItem("client", "firefox");
    query.addQueryItem("ds", "yt");
    query.addQueryItem("q", typed);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(4000);
    QNetworkReply *reply = m_net.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, serial] {
        reply->deleteLater();
        if (serial != m_serial || reply->error() != QNetworkReply::NoError)
            return;
        const QJsonArray answer = QJsonDocument::fromJson(reply->readAll()).array();
        QSet<QString> there;
        for (int i = 0; i < m_list->count(); ++i)
            there.insert(m_list->item(i)->data(Qt::UserRole).toString().toLower());
        int added = 0;
        for (const QJsonValue &v : answer.at(1).toArray()) {
            const QString s = v.toString();
            if (s.isEmpty() || there.contains(s.toLower()))
                continue;
            auto *row = new QListWidgetItem(QString(QChar(0xE721)) + QStringLiteral("    ") + s);
            row->setData(Qt::UserRole, s);
            row->setData(kRowKind, QStringLiteral("suggested"));
            m_list->addItem(row);
            if (++added == 7)
                break;
        }
        layoutCard();
    });
}

void SearchPopup::run(const QString &text)
{
    const QString q = text.trimmed();
    hide();
    if (q.isEmpty())
        return;
    remember(q);
    if (onSearch)
        onSearch(q);
}

void SearchPopup::paintEvent(QPaintEvent *)
{
    // The app dimmed behind the box.
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 120));
}

void SearchPopup::mousePressEvent(QMouseEvent *event)
{
    if (!m_card->geometry().contains(event->position().toPoint()))
        hide();
}

bool SearchPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Escape) {
            hide();
            return true;
        }
        if ((key == Qt::Key_Down || key == Qt::Key_Up) && m_list->count()) {
            int row = m_list->currentItem() && m_list->currentItem()->isSelected() ? m_list->currentRow() : -1;
            row = key == Qt::Key_Down ? qMin(row + 1, m_list->count() - 1) : row - 1;
            if (row < 0) {
                m_list->clearSelection();
                m_list->setCurrentRow(-1);
            } else {
                m_list->setCurrentRow(row);
                m_list->item(row)->setSelected(true);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool SearchPopup::event(QEvent *event)
{
    // Clicked away to another window: it closes, as a menu does.
    if (event->type() == QEvent::WindowDeactivate)
        hide();
    if (event->type() == QEvent::Resize)
        layoutCard();
    return QWidget::event(event);
}
