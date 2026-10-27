#include "mainwindow.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace {

// Dark, with the magenta the installer and Discord card already use.
void applyTheme(QApplication &app)
{
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    const QColor base(24, 24, 28), panel(32, 32, 38), text(230, 230, 235), accent(186, 85, 211);
    p.setColor(QPalette::Window, panel);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, panel);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(44, 44, 52));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(140, 140, 150));
    p.setColor(QPalette::ToolTipBase, panel);
    p.setColor(QPalette::ToolTipText, text);
    app.setPalette(p);
    app.setStyleSheet(
        "QPushButton { padding: 6px 14px; border-radius: 4px; }"
        "QPushButton#primary { background: rgb(186,85,211); color: white; font-weight: bold; }"
        "QListWidget#categories { font-size: 11pt; }"
        "QListWidget#categories::item { padding: 8px; }"
        "QLineEdit { padding: 6px; }");
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("TheTakanosu");
    QApplication::setApplicationName("Ghost Engine");
    QApplication::setApplicationVersion(GHOST_VERSION);
    applyTheme(app);

    MainWindow window;
    window.show();
    return app.exec();
}
