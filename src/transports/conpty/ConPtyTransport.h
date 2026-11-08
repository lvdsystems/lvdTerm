#pragma once

#include <memory>

#include <QString>

#include "core/Transport.h"

// Windows counterpart of PtyTransport (see src/transports/pty): a local
// shell attached to a ConPTY pseudo-console (Windows 10 1809+). Same
// contract - the shell exiting is reported as Disconnected.
//
// The pseudo-console API is looked up from kernel32 at runtime rather
// than linked, so the binary still starts on a Windows too old to have
// it (connecting then just fails with an error message), and doesn't
// depend on which of these declarations the toolchain's headers ship.
//
// Nothing here may block the GUI thread: keystrokes go through a writer thread
// (a pipe write waits for as long as the shell isn't reading), and closing happens
// on a thread of its own (ClosePseudoConsole waits for the console's processes and
// for its output to drain). Output is throttled while the GUI thread catches up.
class ConPtyTransport : public Transport
{
    Q_OBJECT

public:
    // Empty `shell` = powershell.exe (present on every Windows that has ConPTY).
    explicit ConPtyTransport(QString shell = {}, QObject *parent = nullptr);
    ~ConPtyTransport() override;

    void connectToHost() override;
    void disconnectFromHost() override;
    void resize(int cols, int rows) override;

public slots:
    void write(const QByteArray &data) override;

private:
    void shutdown(); // idempotent
    void onChildExited(); // GUI thread, queued from the reader thread

    QString m_shell;
    int m_cols = 80;
    int m_rows = 24;

    // Raw handles stored as void* to keep <windows.h> out of this header.
    void *m_pseudoConsole = nullptr; // HPCON
    void *m_inputWrite = nullptr; // we write keystrokes here
    void *m_outputRead = nullptr; // we read terminal output here
    void *m_process = nullptr;

    struct Shared; // what the helper threads use, which may outlive this object
    std::shared_ptr<Shared> m_shared;
};
