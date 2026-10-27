#include "mainwindow.h"
#include "theme.h"

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>

namespace {

// One window, however many .m3u files are double-clicked: a second start
// hands its files to the first over this pipe and quits.
constexpr const char *kInstancePipe = "ghost-engine-app";

bool handOver(const QStringList &files)
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kInstancePipe));
    if (!socket.waitForConnected(300))
        return false;
    socket.write(files.join('\n').toUtf8());
    socket.waitForBytesWritten(1000);
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("TheTakanosu");
    QApplication::setApplicationName("Ghost Engine");
    QApplication::setApplicationVersion(GHOST_VERSION);

    const QStringList files = app.arguments().mid(1);
    if (handOver(files.isEmpty() ? QStringList{""} : files))
        return 0;

    theme::apply(app);
    MainWindow window;

    QLocalServer server;
    QLocalServer::removeServer(QString::fromLatin1(kInstancePipe));
    server.listen(QString::fromLatin1(kInstancePipe));
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
        QLocalSocket *client = server.nextPendingConnection();
        QObject::connect(client, &QLocalSocket::readyRead, &window, [&window, client] {
            const QStringList paths = QString::fromUtf8(client->readAll()).split('\n', Qt::SkipEmptyParts);
            window.importFiles(paths);
            if (paths.isEmpty()) {
                window.showNormal();
                window.raise();
                window.activateWindow();
            }
            client->deleteLater();
        });
    });

    window.show();
    window.importFiles(files);
    return app.exec();
}
