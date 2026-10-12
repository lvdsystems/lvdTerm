#pragma once

#include <sys/types.h>

#include <QByteArray>
#include <QString>

#include "core/Transport.h"

class QSocketNotifier;

// A local shell on a pseudo-terminal - the "local console" counterpart to
// the remote transports. POSIX only (forkpty) - Windows uses
// ConPtyTransport instead, picked via LocalTransport.h; this isn't even
// compiled there (see src/CMakeLists.txt).
//
// The child is a normal interactive shell (the configured one, else
// $SHELL, else the passwd entry's, else /bin/sh) started in $HOME with
// TERM=xterm-256color, same terminal type SshTransport/TelnetTransport
// advertise. The shell exiting (or the pty closing for any other reason)
// is reported as Disconnected, like a remote end hanging up.
class PtyTransport : public Transport
{
    Q_OBJECT

public:
    // Empty `shell` = the user's login shell.
    explicit PtyTransport(QString shell = {}, QObject *parent = nullptr);
    ~PtyTransport() override;

    void connectToHost() override;
    void disconnectFromHost() override;
    void resize(int cols, int rows) override;

public slots:
    void write(const QByteArray &data) override;

private slots:
    void onReadable();
    void onWritable();

private:
    void flushPending();
    void shutdown(); // closes the pty, hangs up/reaps the child; idempotent

    QString m_shell;
    int m_masterFd = -1;
    pid_t m_pid = -1;
    int m_cols = 80;
    int m_rows = 24;
    QByteArray m_pending; // not yet accepted by the (non-blocking) pty master
    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
};
