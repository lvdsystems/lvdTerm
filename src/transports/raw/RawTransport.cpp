#include "RawTransport.h"

RawTransport::RawTransport(QString host, quint16 port, QObject *parent)
    : Transport(parent)
    , m_host(std::move(host))
    , m_port(port)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::connected, this, &RawTransport::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &RawTransport::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &RawTransport::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &RawTransport::onSocketError);
}

void RawTransport::connectToHost()
{
    setState(State::Connecting);
    m_socket->connectToHost(m_host, m_port);
}

void RawTransport::disconnectFromHost()
{
    m_socket->disconnectFromHost();
}

void RawTransport::resize(int /*cols*/, int /*rows*/) { }

void RawTransport::write(const QByteArray &data)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState)
        return;
    m_socket->write(data);
}

void RawTransport::onConnected()
{
    setState(State::Connected);
}

void RawTransport::onDisconnected()
{
    setState(State::Disconnected);
}

void RawTransport::onReadyRead()
{
    emit readyRead(m_socket->readAll());
}

void RawTransport::onSocketError(QAbstractSocket::SocketError /*error*/)
{
    emit errorOccurred(m_socket->errorString());
    setState(State::Error);
}
