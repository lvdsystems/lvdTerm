#include "SshWorker.h"

#include <QSocketNotifier>
#include <QThread>
#include <QTimer>

#include "SshConnectHelper.h"
#include "SshTransport.h"
#include "X11Forwarder.h"

namespace
{
constexpr qint64 kMaxBytesInFlight = 256 * 1024;
constexpr qint64 kResumeBytesInFlight = 64 * 1024;
constexpr int kResumeCheckMs = 5;
} // namespace

SshWorker::SshWorker(SshConnectionSettings settings, SshTransport *transport, std::shared_ptr<std::atomic<qint64>> bytesInFlight)
    : m_settings(std::move(settings))
    , m_transport(transport)
    , m_bytesInFlight(std::move(bytesInFlight))
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

    // A rejected password is retried in place rather than just failing
    // outright - the user can fix a mistyped password without having to
    // close and reopen the pane. Bounded (matches OpenSSH's own default
    // of 3 total password attempts) since each retry needs a real,
    // human-entered answer - not something that could spin on its own.
    // Only for AuthMethod::Password: retrying with a different password
    // makes no sense for PublicKey auth, and a rejected key is something
    // to fix in the connection's settings, not paper over live.
    constexpr int kMaxPasswordAttempts = 3;
    bool passwordChanged = false;
    for (int attempt = 1; !SshConnectHelper::authenticate(m_session, m_settings, &error); ++attempt) {
        const bool canRetry = m_settings.authMethod == SshConnectionSettings::AuthMethod::Password && attempt < kMaxPasswordAttempts && m_transport;
        if (!canRetry) {
            emit errorOccurred(error);
            teardown();
            emit disconnected();
            return;
        }

        SshCredentialPrompt result;
        QMetaObject::invokeMethod(m_transport, "promptForPassword", Qt::BlockingQueuedConnection, Q_RETURN_ARG(SshCredentialPrompt, result),
                                   Q_ARG(QString, m_settings.host), Q_ARG(QString, m_settings.username));
        if (!result.accepted) {
            emit errorOccurred(QStringLiteral("Cancelled"));
            teardown();
            emit disconnected();
            return;
        }
        m_settings.password = result.value;
        passwordChanged = true;
    }
    if (passwordChanged)
        emit passwordAccepted(m_settings.password);

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

    m_resumeTimer = new QTimer(this);
    connect(m_resumeTimer, &QTimer::timeout, this, &SshWorker::onResumeTimer);

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

    if (m_settings.forwardX11) {
        m_x11 = new X11Forwarder(m_session, this);
        m_x11->requestOn(m_channel); // refusal is fine: no forwarding, shell still works
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
    if (m_x11)
        m_x11->pump();
    for (;;) {
        if (m_bytesInFlight->load() > kMaxBytesInFlight) {
            m_readNotifier->setEnabled(false);
            m_resumeTimer->start(kResumeCheckMs);
            break;
        }

        const ssize_t n = libssh2_channel_read(m_channel, buffer, sizeof(buffer));
        if (n > 0) {
            m_bytesInFlight->fetch_add(n);
            emit dataReceived(QByteArray(buffer, static_cast<int>(n)));
            continue;
        }
        if (n == LIBSSH2_ERROR_EAGAIN)
            break;

        // n == 0 with EOF set, or a real error: the session is over.
        if (n == 0 && !libssh2_channel_eof(m_channel))
            break; // spurious wakeup

        teardown();
        emit disconnected();
        return;
    }

    // What arrived may be the window update a stalled write was waiting for.
    if (!m_pendingWrite.isEmpty())
        pumpWrites();
}

void SshWorker::onResumeTimer()
{
    if (m_bytesInFlight->load() > kResumeBytesInFlight)
        return;

    m_resumeTimer->stop();
    m_readNotifier->setEnabled(true);
    onReadActivity(); // libssh2 may already hold data the socket won't signal again
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
            // Waiting on the socket, or on the server's flow-control window?
            // Only the former is writability; the latter arrives as incoming
            // data (see onReadActivity()), and a write notifier would just spin.
            m_writeNotifier->setEnabled(libssh2_session_block_directions(m_session) & LIBSSH2_SESSION_BLOCK_OUTBOUND);
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
    const int rc = libssh2_keepalive_send(m_session, &secondsToNext);
    if (rc != 0 && rc != LIBSSH2_ERROR_EAGAIN) {
        teardown();
        emit disconnected();
        return;
    }
    if (!m_pendingWrite.isEmpty())
        pumpWrites();
}

void SshWorker::teardown()
{
    m_stage = Stage::Idle;

    // Stopped now, deleted later: this often runs from inside one of their own
    // signals. The notifiers must be off before the socket closes below.
    const auto discardTimer = [](QTimer *&timer) {
        if (timer) {
            timer->stop();
            timer->deleteLater();
            timer = nullptr;
        }
    };
    const auto discardNotifier = [](QSocketNotifier *&notifier) {
        if (notifier) {
            notifier->setEnabled(false);
            notifier->deleteLater();
            notifier = nullptr;
        }
    };
    discardTimer(m_keepaliveTimer);
    discardTimer(m_resumeTimer);
    discardNotifier(m_readNotifier);
    discardNotifier(m_writeNotifier);
    delete m_x11; // frees its channels, so before the session goes
    m_x11 = nullptr;

    if (m_channel) {
        libssh2_channel_free(m_channel);
        m_channel = nullptr;
    }
    if (m_session) {
        libssh2_session_disconnect(m_session, "lvdterm closing");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    SshConnectHelper::closeSocket(m_socket);
    m_socket = LIBSSH2_INVALID_SOCKET;
}
