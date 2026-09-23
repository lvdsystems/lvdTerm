#pragma once

#include <QThread>

#include "SshConnectionSettings.h"
#include "core/Transport.h"

class SshWorker;

// Transport backed by libssh2, running on its own QThread (see
// SshWorker - a libssh2 session cannot safely be driven from the GUI
// thread). This class itself lives on the GUI thread and is a thin,
// thread-safe proxy: every call it makes into SshWorker crosses threads
// via a queued connection.
class SshTransport : public Transport
{
    Q_OBJECT

public:
    explicit SshTransport(SshConnectionSettings settings, QObject *parent = nullptr);
    ~SshTransport() override;

    void connectToHost() override;
    void disconnectFromHost() override;
    void resize(int cols, int rows) override;

    // Called by SshWorker (on its own thread) via a *blocking* queued
    // invocation so it can wait for the answer before proceeding with
    // the handshake. Shows a modal dialog on the GUI thread, which is
    // safe here precisely because this runs on the GUI thread despite
    // being invoked from the worker thread.
    Q_INVOKABLE bool confirmHostKey(const QString &message);

public slots:
    void write(const QByteArray &data) override;

private:
    SshConnectionSettings m_settings;
    QThread m_thread;
    SshWorker *m_worker = nullptr;
};
