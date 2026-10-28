#include "mpvclient.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace {

// What the now-playing bar shows. user-data/ghost/source is the video
// songmatch.lua chose, which a search entry's own path does not carry.
const char *const kObserved[] = {
    "media-title", "pause", "time-pos", "duration", "volume",
    "playlist-pos", "playlist-count", "path", "user-data/ghost/source",
    "metadata/by-key/uploader", "playlist-path", "loop-playlist", "loop-file", "user-data/ghost/audio",
};

} // namespace

MpvClient::MpvClient(QObject *parent) : QObject(parent)
{
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &MpvClient::tryConnect);
    connect(&m_socket, &QLocalSocket::connected, this, &MpvClient::onConnected);
    connect(&m_socket, &QLocalSocket::readyRead, this, &MpvClient::readLines);
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] {
        m_props.clear();
        emit connectionChanged(false);
        m_retry.start();
    });
    m_retry.start();
    tryConnect();
}

void MpvClient::tryConnect()
{
    if (m_socket.state() != QLocalSocket::UnconnectedState)
        return;
    m_socket.connectToServer(QString::fromLatin1(kPipe));
}

void MpvClient::onConnected()
{
    m_retry.stop();
    m_buffer.clear();
    int id = 1;
    for (const char *name : kObserved)
        send({{"command", QVariantList{"observe_property", id++, QString::fromLatin1(name)}}});
    emit connectionChanged(true);
}

void MpvClient::readLines()
{
    m_buffer += m_socket.readAll();
    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        const QJsonObject msg = QJsonDocument::fromJson(line).object();
        if (msg.value("event").toString() != "property-change")
            continue;
        const QString name = msg.value("name").toString();
        const QVariant value = msg.value("data").toVariant();
        m_props.insert(name, value);
        emit propertyChanged(name, value);
    }
}

void MpvClient::send(const QVariantMap &message)
{
    if (!connected())
        return;
    m_socket.write(QJsonDocument(QJsonObject::fromVariantMap(message)).toJson(QJsonDocument::Compact) + '\n');
}

void MpvClient::command(const QVariantList &args)
{
    send({{"command", args}});
}

void MpvClient::setProperty(const QString &name, const QVariant &value)
{
    send({{"command", QVariantList{"set_property", name, value}}});
}
