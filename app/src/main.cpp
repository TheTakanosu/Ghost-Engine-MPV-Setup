#include "mainwindow.h"
#include "theme.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("TheTakanosu");
    QApplication::setApplicationName("Ghost Engine");
    QApplication::setApplicationVersion(GHOST_VERSION);
    theme::apply(app);

    MainWindow window;
    window.show();
    return app.exec();
}
