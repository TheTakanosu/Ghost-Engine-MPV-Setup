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
        QWidget#sidebar { background: rgb(17,16,21); }
        QLabel#brand { font-size: 15pt; font-weight: 700; color: rgb(207,179,240); padding: 4px 8px; }
        QListWidget#nav { background: transparent; border: none; outline: none; font-size: 11pt; }
        QListWidget#nav::item { padding: 10px 12px; border-radius: 10px; margin: 2px 0; color: rgb(190,188,200); }
        QListWidget#nav::item:hover { background: rgb(33,32,41); color: rgb(236,236,241); }
        QListWidget#nav::item:selected { background: rgb(52,42,74); color: rgb(236,236,241); }
        QListWidget#grid { background: transparent; border: none; outline: none; }
        QLabel#pageTitle { font-size: 22pt; font-weight: 700; }
        QLabel#sectionTitle { font-size: 14pt; font-weight: 600; padding-top: 6px; }
        QWidget#statCard { background: rgb(33,32,41); border-radius: 14px; }
        QLabel#statValue { font-size: 22pt; font-weight: 700; color: rgb(207,179,240); }
        QLabel#statLabel { color: rgb(154,152,166); }
        QWidget#nowPlaying { background: rgb(27,26,33); border-top: 1px solid rgb(44,42,54); }
        QLabel#npTitle { font-size: 11pt; font-weight: 600; }
        QLabel#npSubtitle, QLabel#npTime { color: rgb(154,152,166); font-size: 9pt; }
        QLabel#npIcon { font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; color: rgb(154,152,166); font-size: 13pt; }
        QPushButton#transport, QPushButton#transportBig {
            font-family: "Segoe Fluent Icons", "Segoe MDL2 Assets"; background: transparent; border: none;
            color: rgb(220,218,228); font-size: 14pt; padding: 4px;
        }
        QPushButton#transport:hover { color: white; }
        QPushButton#transportBig {
            background: rgb(236,236,241); color: rgb(22,21,27); border-radius: 18px;
            min-width: 36px; max-width: 36px; min-height: 36px; max-height: 36px; font-size: 13pt; padding: 0;
        }
        QPushButton#transportBig:hover { background: white; }
        QSlider::groove:horizontal { height: 4px; background: rgb(60,58,72); border-radius: 2px; }
        QSlider::sub-page:horizontal { background: rgb(176,127,224); border-radius: 2px; }
        QSlider::handle:horizontal { background: white; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }
        QPushButton {
            background: rgb(33,32,41); border: 1px solid rgb(52,50,64);
            border-radius: 10px; padding: 8px 16px; color: rgb(236,236,241);
        }
        QPushButton:hover { background: rgb(42,40,53); }
        QLineEdit {
            background: rgb(33,32,41); border: 1px solid rgb(52,50,64);
            border-radius: 18px; padding: 8px 16px; selection-background-color: rgb(176,127,224);
        }
        QLineEdit:focus { border-color: rgb(176,127,224); }
        QSplitter::handle { background: rgb(17,16,21); }
        QSplitter::handle:hover { background: rgb(60,48,84); }
        QScrollArea { background: transparent; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 4px; }
        QScrollBar::handle:vertical { background: rgb(60,58,72); border-radius: 4px; min-height: 30px; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
        QScrollBar::handle:horizontal { background: rgb(60,58,72); border-radius: 4px; min-width: 30px; }
        QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
        QMenu { background: rgb(33,32,41); border: 1px solid rgb(52,50,64); border-radius: 8px; padding: 4px; }
        QMenu::item { padding: 6px 18px; border-radius: 6px; }
        QMenu::item:selected { background: rgb(60,48,84); }
    )"));
}

} // namespace theme
