#pragma once

#include <QHash>
#include <QTcpSocket>

#include "core/Transport.h"

// A plain (unencrypted) Telnet client transport on QTcpSocket. Implements
// just enough option negotiation (RFC 854/855) to be usable against real
// devices: ECHO/SGA (accept the server's remote-echo model — our terminal
// core never echoes locally, so this is really just protocol politeness),
// NAWS (report window size, RFC 1073) and TERMINAL-TYPE (RFC 1091).
class TelnetTransport : public Transport
{
    Q_OBJECT

public:
    TelnetTransport(QString host, quint16 port, QObject *parent = nullptr);

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
    // Telnet command/option bytes (RFC 854).
    enum : quint8
    {
        SE = 240,
        SB = 250,
        WILL = 251,
        WONT = 252,
        DO = 253,
        DONT = 254,
        IAC = 255,
    };
    enum : quint8
    {
        OPT_ECHO = 1,
        OPT_SGA = 3,
        OPT_TTYPE = 24,
        OPT_NAWS = 31,
    };

    enum class ParseState
    {
        Data,
        IacSeen,
        Negotiate, // saw IAC WILL/WONT/DO/DONT, waiting for the option byte
        SubNegData,
        SubNegIacSeen,
    };

    void processIncoming(const QByteArray &chunk);
    void handleCommand(quint8 command, quint8 option);
    void handleSubnegotiation(const QByteArray &payload);
    void sendCommand(quint8 command, quint8 option);
    void sendSubnegotiation(quint8 option, const QByteArray &payload);
    void sendWindowSize();

    QString m_host;
    quint16 m_port;
    QTcpSocket *m_socket = nullptr;

    ParseState m_parseState = ParseState::Data;
    quint8 m_pendingCommand = 0;
    QByteArray m_subNegBuffer;
    QByteArray m_plainBuffer;

    bool m_nawsEnabled = false;
    int m_cols = 80;
    int m_rows = 24;
};
