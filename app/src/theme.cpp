#include "theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace theme {

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

void apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create("Fusion"));
    QFont font("Segoe UI", 10);
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

    app.setStyleSheet(QStringLiteral(R"(
        QPushButton {
            background: rgb(33,32,41); border: 1px solid rgb(52,50,64);
            border-radius: 10px; padding: 8px 16px; color: rgb(236,236,241);
        }
        QPushButton:hover { background: rgb(42,40,53); }
        QPushButton#primary {
            background: rgb(176,127,224); border: none; color: rgb(24,18,34); font-weight: 600;
        }
        QPushButton#primary:hover { background: rgb(190,146,234); }
        QPushButton#chip { border-radius: 15px; padding: 6px 16px; }
        QPushButton#chip:checked {
            background: rgb(60,48,84); border-color: rgb(176,127,224); color: rgb(225,205,248);
        }
        QLineEdit {
            background: rgb(33,32,41); border: 1px solid rgb(52,50,64);
            border-radius: 10px; padding: 8px 12px; selection-background-color: rgb(176,127,224);
        }
        QLineEdit:focus { border-color: rgb(176,127,224); }
        QListWidget#cards { background: transparent; border: none; outline: none; }
        QLabel#heroTitle { font-size: 20pt; font-weight: 600; }
        QLabel#heroSubtitle { color: rgb(154,152,166); font-size: 10.5pt; }
        QLabel#status { color: rgb(120,118,132); font-size: 9pt; }
        QScrollBar:vertical { background: transparent; width: 8px; margin: 4px; }
        QScrollBar::handle:vertical { background: rgb(60,58,72); border-radius: 4px; min-height: 30px; }
        QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
        QMenu { background: rgb(33,32,41); border: 1px solid rgb(52,50,64); border-radius: 8px; padding: 4px; }
        QMenu::item { padding: 6px 18px; border-radius: 6px; }
        QMenu::item:selected { background: rgb(60,48,84); }
    )"));
}

} // namespace theme
