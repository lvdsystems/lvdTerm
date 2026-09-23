#pragma once

#include <QTcpSocket>

#include "core/Transport.h"

// A plain TCP connection with no protocol layered on top at all - PuTTY's
// "Raw" connection type. Unlike TelnetTransport (see
// src/transports/telnet/TelnetTransport.h), bytes pass through completely
// unmodified in both directions: no IAC parsing, no option negotiation, no
// 0xFF escaping. resize() is a no-op, the same as LoopbackTransport's -
// raw TCP has no concept of a terminal window size to report.
class RawTransport : public Transport
{
    Q_OBJECT

public:
    RawTransport(QString host, quint16 port, QObject *parent = nullptr);

    void connectToHost() override;
    void disconnectFromHost() override;
    void resize(int cols, int rows) override;

public slots:
    void write(const QByteArray &data) override;

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    QString m_host;
    quint16 m_port;
    QTcpSocket *m_socket = nullptr;
};
