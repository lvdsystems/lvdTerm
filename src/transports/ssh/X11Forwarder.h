#pragma once

#include <libssh2.h>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

class QIODevice;
class QTimer;

// Handles the client side of SSH X11 forwarding for one SshWorker: the
// server opens an "x11" channel for every X client started on the
// remote host, and each one is bridged here to the local X server named
// by $DISPLAY (a unix socket on Linux, TCP localhost:6000+N otherwise).
//
// Lives on the worker thread and is driven from there: libssh2 channels
// are not thread-safe, so everything touching them happens in pump(),
// called from the worker's own read activity and a short poll timer.
//
// The remote side only ever sees a throwaway "fake" cookie (sent in the
// x11-req). The first packet from each remote X client is rewritten to
// carry the *real* local cookie (read via xauth) before it reaches the
// local X server, so the real one never leaves this machine.
class X11Forwarder : public QObject
{
    Q_OBJECT

public:
    explicit X11Forwarder(LIBSSH2_SESSION *session, QObject *parent = nullptr);
    ~X11Forwarder() override;

    // Sends the x11-req on an already-open session channel. False if the
    // server refused (not fatal - the shell works without it).
    bool requestOn(LIBSSH2_CHANNEL *channel);

public slots:
    void pump();

private:
    struct Connection
    {
        LIBSSH2_CHANNEL *channel = nullptr;
        QIODevice *socket = nullptr;
        QByteArray setup;    // remote's connection-setup bytes, until complete
        bool setupDone = false;
        QByteArray toRemote; // local X server -> remote, waiting on the channel window
        QByteArray toLocal;  // remote -> local X server, waiting on the socket
    };

    static void onChannelOpen(LIBSSH2_SESSION *session, LIBSSH2_CHANNEL *channel, const char *host, int port, void **abstract);
    void adopt(LIBSSH2_CHANNEL *channel);
    bool processSetup(Connection &c);
    static bool localClosed(const Connection &c);
    void closeConnection(Connection &c);
    void loadLocalDisplay();

    LIBSSH2_SESSION *m_session;
    QTimer *m_pollTimer = nullptr;
    QList<Connection> m_connections;

    QByteArray m_fakeCookie; // 16 raw bytes
    QByteArray m_realCookie; // 16 raw bytes, empty if none found

    // Where the local X server is.
    QString m_unixPath;
    QString m_tcpHost;
    quint16 m_tcpPort = 0;
};
