#pragma once

#include <libssh2.h>

#include <atomic>
#include <memory>

#include <QByteArray>
#include <QObject>
#include <QPointer>

#include "SshConnectionSettings.h"

class QSocketNotifier;
class QTimer;
class SshTransport;
class X11Forwarder;

// Does all the actual (blocking, by nature) libssh2 work: TCP connect,
// handshake, host key verification, auth, pty/shell channel setup, then
// a non-blocking read/write pump driven by QSocketNotifier. Lives on its
// own QThread (see SshTransport), never the GUI thread - a libssh2
// session is not thread-safe and none of this can safely run on the UI
// thread's event loop without freezing the app.
//
// Cross-thread contract: every public slot below is called by
// SshTransport via a queued connection (SshTransport lives on the GUI
// thread, this object on the worker thread). Signals are likewise
// queued back. The one exception is host key verification, which uses a
// *blocking* queued call into SshTransport::confirmHostKey() so this
// thread can wait for the user's answer without needing its own dialog
// (see start()).
class SshWorker : public QObject
{
    Q_OBJECT

public:
    // `bytesInFlight`: what has been emitted through dataReceived() and not yet
    // processed by the receiver (which subtracts it again). Reading pauses while
    // this is high. Without a limit, a burst of output floods the GUI thread with
    // queued events faster than it can handle them, and Qt delivers those before
    // any input or painting: the whole application freezes for as long as the
    // output lasts.
    SshWorker(SshConnectionSettings settings, SshTransport *transport, std::shared_ptr<std::atomic<qint64>> bytesInFlight);
    ~SshWorker() override;

public slots:
    void start();
    void stop();
    void enqueueWrite(const QByteArray &data);
    void requestResize(int cols, int rows);

signals:
    void dataReceived(const QByteArray &data);
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);
    // A password typed at the "wrong password" prompt worked.
    void passwordAccepted(const QString &password);

private slots:
    void onReadActivity();
    void onWriteActivity();
    void onKeepaliveTimer();
    void onResumeTimer();

private:
    enum class Stage
    {
        Idle,
        Connected,   // TCP + handshake + auth + shell all done
        Failed,
    };

    bool openShellChannel();
    void teardown();
    void pumpWrites();

    SshConnectionSettings m_settings;
    QPointer<SshTransport> m_transport; // for the blocking host-key prompt only

    libssh2_socket_t m_socket = LIBSSH2_INVALID_SOCKET;
    LIBSSH2_SESSION *m_session = nullptr;
    LIBSSH2_CHANNEL *m_channel = nullptr;

    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QTimer *m_keepaliveTimer = nullptr;
    QTimer *m_resumeTimer = nullptr; // reading paused, see bytesInFlight
    std::shared_ptr<std::atomic<qint64>> m_bytesInFlight;
    X11Forwarder *m_x11 = nullptr;

    QByteArray m_pendingWrite;
    Stage m_stage = Stage::Idle;
    int m_cols = 80;
    int m_rows = 24;
};
