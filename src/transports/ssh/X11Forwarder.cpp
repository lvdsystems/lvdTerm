#include "X11Forwarder.h"

#include <QLocalSocket>
#include <QProcess>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>

namespace
{
constexpr int kCookieLen = 16;
const char kAuthProto[] = "MIT-MAGIC-COOKIE-1";

int pad4(int n)
{
    return (n + 3) & ~3;
}

quint16 readU16(const QByteArray &b, int off, bool little)
{
    const auto lo = static_cast<quint8>(b[off]);
    const auto hi = static_cast<quint8>(b[off + 1]);
    return little ? static_cast<quint16>(lo | (hi << 8)) : static_cast<quint16>((lo << 8) | hi);
}

void writeU16(QByteArray &b, int off, quint16 v, bool little)
{
    b[off] = static_cast<char>(little ? (v & 0xff) : (v >> 8));
    b[off + 1] = static_cast<char>(little ? (v >> 8) : (v & 0xff));
}
} // namespace

X11Forwarder::X11Forwarder(LIBSSH2_SESSION *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    for (int i = 0; i < kCookieLen; ++i)
        m_fakeCookie.append(static_cast<char>(QRandomGenerator::system()->bounded(256)));

    loadLocalDisplay();

    *libssh2_session_abstract(m_session) = this;
    libssh2_session_callback_set(m_session, LIBSSH2_CALLBACK_X11, reinterpret_cast<void *>(&X11Forwarder::onChannelOpen));

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout, this, &X11Forwarder::pump);
}

X11Forwarder::~X11Forwarder()
{
    for (Connection &c : m_connections)
        closeConnection(c);
    m_connections.clear();
    *libssh2_session_abstract(m_session) = nullptr;
}

void X11Forwarder::loadLocalDisplay()
{
    QString display = qEnvironmentVariable("DISPLAY");
    if (display.isEmpty()) {
#ifdef Q_OS_WIN
        display = QStringLiteral("localhost:0"); // VcXsrv / X410 / similar
#else
        display = QStringLiteral(":0");
#endif
    }

    const int colon = display.lastIndexOf(QLatin1Char(':'));
    if (colon < 0)
        return;
    const QString host = display.left(colon);
    const int number = display.mid(colon + 1).section(QLatin1Char('.'), 0, 0).toInt();

#ifndef Q_OS_WIN
    if (host.isEmpty() || host == QLatin1String("unix")) {
        m_unixPath = QStringLiteral("/tmp/.X11-unix/X%1").arg(number);
    } else
#endif
    {
        m_tcpHost = host.isEmpty() ? QStringLiteral("localhost") : host;
        m_tcpPort = static_cast<quint16>(6000 + number);
    }

    // The real cookie, if the local server wants one. A missing xauth or
    // an empty list just means we connect without credentials (works for
    // servers started with access control off, e.g. VcXsrv "-ac").
    QProcess xauth;
    xauth.start(QStringLiteral("xauth"), {QStringLiteral("list"), display});
    if (!xauth.waitForFinished(2000))
        return;
    const QList<QByteArray> lines = xauth.readAllStandardOutput().split('\n');
    for (const QByteArray &line : lines) {
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() >= 3 && fields[1] == kAuthProto) {
            const QByteArray raw = QByteArray::fromHex(fields[2]);
            if (raw.size() == kCookieLen) {
                m_realCookie = raw;
                return;
            }
        }
    }
}

bool X11Forwarder::requestOn(LIBSSH2_CHANNEL *channel)
{
    const QByteArray hex = m_fakeCookie.toHex();
    return libssh2_channel_x11_req_ex(channel, 0, kAuthProto, hex.constData(), 0) == 0;
}

void X11Forwarder::onChannelOpen(LIBSSH2_SESSION *session, LIBSSH2_CHANNEL *channel, const char *, int, void **)
{
    auto *self = static_cast<X11Forwarder *>(*libssh2_session_abstract(session));
    if (self)
        self->adopt(channel);
    else
        libssh2_channel_free(channel);
}

void X11Forwarder::adopt(LIBSSH2_CHANNEL *channel)
{
    Connection c;
    c.channel = channel;

    if (!m_unixPath.isEmpty()) {
        auto *s = new QLocalSocket(this);
        connect(s, &QLocalSocket::readyRead, this, &X11Forwarder::pump);
        connect(s, &QLocalSocket::disconnected, this, &X11Forwarder::pump);
        s->connectToServer(m_unixPath);
        c.socket = s;
    } else {
        auto *s = new QTcpSocket(this);
        connect(s, &QTcpSocket::readyRead, this, &X11Forwarder::pump);
        connect(s, &QTcpSocket::disconnected, this, &X11Forwarder::pump);
        connect(s, &QTcpSocket::errorOccurred, this, &X11Forwarder::pump);
        s->connectToHost(m_tcpHost, m_tcpPort);
        c.socket = s;
    }

    m_connections.append(c);
    if (!m_pollTimer->isActive())
        m_pollTimer->start(10);
}

// The remote X client's first bytes are the connection-setup request:
// byte order, pad, major/minor version, auth-name length, auth-data
// length, pad, then name and data each padded to 4 bytes. Swap in the
// real credentials; refuse clients that didn't present our fake cookie.
bool X11Forwarder::processSetup(Connection &c)
{
    if (c.setup.size() < 12)
        return true; // wait for more
    const bool little = c.setup[0] == 'l';
    if (!little && c.setup[0] != 'B')
        return false;

    const int nameLen = readU16(c.setup, 6, little);
    const int dataLen = readU16(c.setup, 8, little);
    const int total = 12 + pad4(nameLen) + pad4(dataLen);
    if (c.setup.size() < total)
        return true;

    const QByteArray data = c.setup.mid(12 + pad4(nameLen), dataLen);
    if (data != m_fakeCookie)
        return false;

    QByteArray out = c.setup.left(12);
    if (m_realCookie.isEmpty()) {
        writeU16(out, 6, 0, little);
        writeU16(out, 8, 0, little);
    } else {
        const QByteArray name(kAuthProto);
        writeU16(out, 6, static_cast<quint16>(name.size()), little);
        writeU16(out, 8, static_cast<quint16>(m_realCookie.size()), little);
        out.append(name);
        out.append(QByteArray(pad4(name.size()) - name.size(), '\0'));
        out.append(m_realCookie);
        out.append(QByteArray(pad4(m_realCookie.size()) - m_realCookie.size(), '\0'));
    }
    out.append(c.setup.mid(total)); // anything already pipelined after setup

    c.toLocal.append(out);
    c.setup.clear();
    c.setupDone = true;
    return true;
}

bool X11Forwarder::localClosed(const Connection &c)
{
    if (const auto *tcp = qobject_cast<const QTcpSocket *>(c.socket))
        return tcp->state() == QAbstractSocket::UnconnectedState;
    if (const auto *local = qobject_cast<const QLocalSocket *>(c.socket))
        return local->state() == QLocalSocket::UnconnectedState;
    return true;
}

void X11Forwarder::closeConnection(Connection &c)
{
    if (c.socket) {
        c.socket->disconnect(this);
        c.socket->deleteLater();
        c.socket = nullptr;
    }
    if (c.channel) {
        libssh2_channel_free(c.channel);
        c.channel = nullptr;
    }
}

void X11Forwarder::pump()
{
    char buffer[16384];

    for (int i = 0; i < m_connections.size();) {
        Connection &c = m_connections[i];
        bool dead = false;

        // remote -> local
        for (;;) {
            const ssize_t n = libssh2_channel_read(c.channel, buffer, sizeof(buffer));
            if (n > 0) {
                if (c.setupDone) {
                    c.toLocal.append(buffer, static_cast<int>(n));
                } else {
                    c.setup.append(buffer, static_cast<int>(n));
                    if (!processSetup(c)) {
                        dead = true;
                        break;
                    }
                }
                continue;
            }
            if (n == LIBSSH2_ERROR_EAGAIN)
                break;
            // n == 0: only a real end if the remote sent EOF.
            if (n < 0 || libssh2_channel_eof(c.channel))
                dead = true;
            break;
        }

        const bool closed = localClosed(c);
        if (!dead && !closed && c.socket->isOpen()) {
            if (!c.toLocal.isEmpty()) {
                const qint64 w = c.socket->write(c.toLocal);
                if (w > 0)
                    c.toLocal.remove(0, static_cast<int>(w));
            }
            c.toRemote.append(c.socket->readAll());
        }

        // local -> remote
        while (!dead && !c.toRemote.isEmpty()) {
            const ssize_t n = libssh2_channel_write(c.channel, c.toRemote.constData(), static_cast<size_t>(c.toRemote.size()));
            if (n == LIBSSH2_ERROR_EAGAIN)
                break;
            if (n < 0) {
                dead = true;
                break;
            }
            c.toRemote.remove(0, static_cast<int>(n));
        }

        // Local side gone (refused, or the X client's server quit) and
        // everything it sent has been forwarded: end the channel.
        if (closed && c.toRemote.isEmpty())
            dead = true;

        if (dead) {
            closeConnection(c);
            m_connections.removeAt(i);
        } else {
            ++i;
        }
    }

    if (m_connections.isEmpty())
        m_pollTimer->stop();
}
