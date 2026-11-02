#pragma once

#include <QJsonValue>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <QVariant>

// Talks to a running mpv over its JSON IPC pipe, the one mpv.conf opens with
// input-ipc-server=ghost-mpv. Connects whenever mpv appears, so the app and
// the player can be started in either order.
class MpvClient : public QObject {
    Q_OBJECT

public:
    static constexpr const char *kPipe = "ghost-mpv";

    explicit MpvClient(QObject *parent = nullptr);

    bool connected() const { return m_socket.state() == QLocalSocket::ConnectedState; }
    QVariant property(const QString &name) const { return m_props.value(name); }

    void command(const QVariantList &args);
    void setProperty(const QString &name, const QVariant &value);
    // Waits (briefly) until what was sent has left, for a command sent
    // just before the app quits.
    void flush() { m_socket.waitForBytesWritten(300); }
    // An mpv was just started: look for it every fifth of a second for a
    // while instead of every two seconds.
    void hurry();

signals:
    void connectionChanged(bool connected);
    void propertyChanged(const QString &name, const QVariant &value);
    // An entry mpv could not open (each of ghost.lua's retries counts), and
    // one that did open.
    void fileFailed(int entryId);
    void fileLoaded();

private:
    void tryConnect();
    void onConnected();
    void readLines();
    void send(const QVariantMap &message);

    QLocalSocket m_socket;
    QTimer m_retry;
    QByteArray m_buffer;
    QVariantMap m_props;
};
