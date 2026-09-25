#include "SshWorker.h"

#include <winsock2.h>

#include <QSocketNotifier>
#include <QThread>
#include <QTimer>

#include "SshConnectHelper.h"
#include "SshTransport.h"

SshWorker::SshWorker(SshConnectionSettings settings, SshTransport *transport)
    : m_settings(std::move(settings))
    , m_transport(transport)
{
}

SshWorker::~SshWorker()
{
    teardown();
}

void SshWorker::start()
{
    QString error;

    m_socket = SshConnectHelper::connectSocket(m_settings.host, m_settings.port, &error);
    if (m_socket == LIBSSH2_INVALID_SOCKET) {
        emit errorOccurred(error);
        teardown();
        emit disconnected();
        return;
    }

    m_session = libssh2_session_init();
    if (!m_session) {
        emit errorOccurred(QStringLiteral("Could not allocate SSH session"));
        teardown();
        emit disconnected();
        return;
    }

    if (!SshConnectHelper::handshakeAndVerifyHostKey(m_session, m_socket, m_settings.host, m_settings.port, m_transport, &error)) {
        emit errorOccurred(error);
        teardown();
        emit disconnected();
        return;
    }

    if (!SshConnectHelper::authenticate(m_session, m_settings, &error)) {
        emit errorOccurred(error);
        teardown();
        emit disconnected();
        return;
    }

    if (!openShellChannel()) {
        // openShellChannel() emits its own errorOccurred() on failure.
        teardown();
        emit disconnected();
        return;
    }

    m_stage = Stage::Connected;

    // See SftpClient::connectToHost() for why this matters there - here
    // it's cheap defense in depth rather than a fix for an observed bug:
    // switching to a non-blocking session below makes SO_RCVTIMEO/
    // SO_SNDTIMEO moot for every socket call this class makes from this
    // point on (non-blocking send()/recv() never wait long enough to hit
    // them), but there's no reason to leave a stale 15s bound on the
    // socket regardless.
    SshConnectHelper::clearSocketTimeouts(m_socket);

    libssh2_session_set_blocking(m_session, 0);

    m_readNotifier = new QSocketNotifier(static_cast<qintptr>(m_socket), QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &SshWorker::onReadActivity);

    m_writeNotifier = new QSocketNotifier(static_cast<qintptr>(m_socket), QSocketNotifier::Write, this);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &SshWorker::onWriteActivity);
    m_writeNotifier->setEnabled(false); // only while m_pendingWrite is non-empty

    // Ask the server to ack keepalives so dead connections are detected
    // rather than hanging forever; libssh2_keepalive_send() must still be
    // called periodically by us to actually emit them.
    libssh2_keepalive_config(m_session, 1, 30);
    m_keepaliveTimer = new QTimer(this);
    connect(m_keepaliveTimer, &QTimer::timeout, this, &SshWorker::onKeepaliveTimer);
    m_keepaliveTimer->start(15000);

    emit connected();
}

void SshWorker::stop()
{
    teardown();
    emit disconnected();
}

bool SshWorker::openShellChannel()
{
    m_channel = libssh2_channel_open_session(m_session);
    if (!m_channel) {
        emit errorOccurred(QStringLiteral("Could not open channel"));
        return false;
    }

    libssh2_channel_handle_extended_data2(m_channel, LIBSSH2_CHANNEL_EXTENDED_DATA_MERGE);

    static const char kTermType[] = "xterm-256color";
    // sizeof(kTermType) - 1, not sizeof(kTermType): the length libssh2
    // wants is strlen(), and including the terminating NUL sends it as a
    // literal trailing byte in the SSH_MSG_CHANNEL_REQUEST string (caught
    // by a test server logging the term type as "xterm-256color\x00").
    if (libssh2_channel_request_pty_ex(m_channel, kTermType, sizeof(kTermType) - 1, nullptr, 0, m_cols, m_rows, 0, 0) != 0) {
        emit errorOccurred(QStringLiteral("Could not request a pty"));
        return false;
    }

    if (libssh2_channel_shell(m_channel) != 0) {
        emit errorOccurred(QStringLiteral("Could not start a shell"));
        return false;
    }

    return true;
}

void SshWorker::enqueueWrite(const QByteArray &data)
{
    if (m_stage != Stage::Connected)
        return;

    m_pendingWrite.append(data);
    pumpWrites();
}

void SshWorker::requestResize(int cols, int rows)
{
    m_cols = cols;
    m_rows = rows;

    if (m_stage != Stage::Connected || !m_channel)
        return;

    // Best-effort: retry briefly on EAGAIN rather than dropping the
    // resize, but never block the worker thread's event loop for long.
    for (int attempt = 0; attempt < 20; ++attempt) {
        const int rc = libssh2_channel_request_pty_size(m_channel, cols, rows);
        if (rc != LIBSSH2_ERROR_EAGAIN)
            break;
        QThread::msleep(5);
    }
}

void SshWorker::onReadActivity()
{
    if (m_stage != Stage::Connected)
        return;

    char buffer[16384];
    for (;;) {
        const ssize_t n = libssh2_channel_read(m_channel, buffer, sizeof(buffer));
        if (n > 0) {
            emit dataReceived(QByteArray(buffer, static_cast<int>(n)));
            continue;
        }
        if (n == LIBSSH2_ERROR_EAGAIN)
            return;

        // n == 0 with EOF set, or a real error: the session is over.
        if (n == 0 && !libssh2_channel_eof(m_channel))
            return; // spurious wakeup

        teardown();
        emit disconnected();
        return;
    }
}

void SshWorker::onWriteActivity()
{
    pumpWrites();
}

void SshWorker::pumpWrites()
{
    while (!m_pendingWrite.isEmpty()) {
        const ssize_t n = libssh2_channel_write(m_channel, m_pendingWrite.constData(), static_cast<size_t>(m_pendingWrite.size()));
        if (n == LIBSSH2_ERROR_EAGAIN) {
            m_writeNotifier->setEnabled(true);
            return;
        }
        if (n < 0) {
            teardown();
            emit disconnected();
            return;
        }
        m_pendingWrite.remove(0, static_cast<int>(n));
    }
    m_writeNotifier->setEnabled(false);
}

void SshWorker::onKeepaliveTimer()
{
    if (m_stage != Stage::Connected)
        return;

    int secondsToNext = 0;
    libssh2_keepalive_send(m_session, &secondsToNext);
}

void SshWorker::teardown()
{
    m_stage = Stage::Idle;

    delete m_keepaliveTimer;
    m_keepaliveTimer = nullptr;
    delete m_readNotifier;
    m_readNotifier = nullptr;
    delete m_writeNotifier;
    m_writeNotifier = nullptr;

    if (m_channel) {
        libssh2_channel_free(m_channel);
        m_channel = nullptr;
    }
    if (m_session) {
        libssh2_session_disconnect(m_session, "lvdterm closing");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    if (m_socket != LIBSSH2_INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = LIBSSH2_INVALID_SOCKET;
        WSACleanup();
    }
}
