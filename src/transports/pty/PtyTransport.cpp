#include "PtyTransport.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <pty.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <QByteArrayList>
#include <QSocketNotifier>

extern char **environ;

namespace
{
QByteArray resolveShell(const QString &configured)
{
    if (!configured.isEmpty())
        return configured.toLocal8Bit();

    if (const char *env = std::getenv("SHELL"); env && *env)
        return env;

    if (const passwd *pw = getpwuid(getuid()); pw && pw->pw_shell && *pw->pw_shell)
        return pw->pw_shell;

    return "/bin/sh";
}

QByteArray resolveHome()
{
    if (const char *env = std::getenv("HOME"); env && *env)
        return env;

    if (const passwd *pw = getpwuid(getuid()); pw && pw->pw_dir && *pw->pw_dir)
        return pw->pw_dir;

    return "/";
}

// Reaping off the GUI thread: waitpid() on a child that ignores SIGHUP
// (a shell stuck on a foreground job, say) would otherwise freeze the UI
// on tab close.
void reapInBackground(pid_t pid)
{
    std::thread([pid] {
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }).detach();
}
} // namespace

PtyTransport::PtyTransport(QString shell, QObject *parent) : Transport(parent), m_shell(std::move(shell)) {}

PtyTransport::~PtyTransport()
{
    shutdown();
}

void PtyTransport::connectToHost()
{
    if (m_masterFd >= 0)
        return;

    setState(State::Connecting);

    // Everything the child needs is prepared before fork(): between
    // forkpty() and execve() only async-signal-safe calls are allowed
    // (this process is multithreaded - SSH/SFTP workers - so malloc/
    // setenv in the child could deadlock on a lock another thread held
    // at fork time).
    const QByteArray shell = resolveShell(m_shell);
    const QByteArray home = resolveHome();

    QByteArrayList env;
    for (char **e = environ; e && *e; ++e) {
        const QByteArray entry(*e);
        if (!entry.startsWith("TERM=") && !entry.startsWith("COLUMNS=") && !entry.startsWith("LINES="))
            env.append(entry);
    }
    env.append("TERM=xterm-256color");

    std::vector<char *> envp;
    for (QByteArray &entry : env)
        envp.push_back(entry.data());
    envp.push_back(nullptr);

    char *argv[] = {const_cast<char *>(shell.constData()), nullptr};

    winsize ws{};
    ws.ws_col = static_cast<unsigned short>(m_cols);
    ws.ws_row = static_cast<unsigned short>(m_rows);

    int masterFd = -1;
    const pid_t pid = forkpty(&masterFd, nullptr, nullptr, &ws);
    if (pid < 0) {
        emit errorOccurred(QStringLiteral("Could not start a local terminal (forkpty failed: %1)").arg(QString::fromLocal8Bit(strerror(errno))));
        setState(State::Error);
        return;
    }

    if (pid == 0) {
        // The app ignores SIGPIPE process-wide (see main()), and ignored
        // dispositions survive exec - restore it, or `yes | head` in the
        // shell would misbehave. Same for whatever signal mask this
        // thread happened to have.
        signal(SIGPIPE, SIG_DFL);
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, nullptr);

        if (chdir(home.constData()) != 0) {
            // Stay in the inherited cwd - not worth failing the shell over.
        }
        execve(shell.constData(), argv, envp.data());
        _exit(127);
    }

    m_masterFd = masterFd;
    m_pid = pid;

    fcntl(m_masterFd, F_SETFL, fcntl(m_masterFd, F_GETFL) | O_NONBLOCK);
    fcntl(m_masterFd, F_SETFD, fcntl(m_masterFd, F_GETFD) | FD_CLOEXEC);

    m_readNotifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &PtyTransport::onReadable);

    m_writeNotifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &PtyTransport::onWritable);

    setState(State::Connected);
}

void PtyTransport::disconnectFromHost()
{
    shutdown();
    setState(State::Disconnected);
}

void PtyTransport::resize(int cols, int rows)
{
    m_cols = cols;
    m_rows = rows;

    if (m_masterFd < 0)
        return;

    winsize ws{};
    ws.ws_col = static_cast<unsigned short>(cols);
    ws.ws_row = static_cast<unsigned short>(rows);
    ioctl(m_masterFd, TIOCSWINSZ, &ws); // the kernel delivers SIGWINCH to the shell's foreground job
}

void PtyTransport::write(const QByteArray &data)
{
    if (m_masterFd < 0)
        return;

    m_pending.append(data);
    flushPending();
}

void PtyTransport::flushPending()
{
    while (!m_pending.isEmpty()) {
        const ssize_t n = ::write(m_masterFd, m_pending.constData(), static_cast<size_t>(m_pending.size()));
        if (n > 0) {
            m_pending.remove(0, static_cast<int>(n));
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            m_writeNotifier->setEnabled(true); // the shell isn't reading right now (big paste) - resume when it is
            return;
        }
        break; // EIO etc.: the child is gone; onReadable() sees the hangup and reports it
    }
    m_writeNotifier->setEnabled(false);
}

void PtyTransport::onWritable()
{
    flushPending();
}

void PtyTransport::onReadable()
{
    char buffer[16384];
    for (;;) {
        const ssize_t n = ::read(m_masterFd, buffer, sizeof(buffer));
        if (n > 0) {
            emit readyRead(QByteArray(buffer, static_cast<int>(n)));
            if (m_masterFd < 0)
                return; // a slot reacted to the data by tearing us down
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return; // drained for now

        // 0 or EIO: the slave side is closed - the shell exited.
        shutdown();
        setState(State::Disconnected);
        return;
    }
}

void PtyTransport::shutdown()
{
    if (m_masterFd < 0)
        return;

    // deleteLater(): shutdown() can run from inside onReadable(), i.e.
    // from within the read notifier's own activated() signal.
    m_readNotifier->setEnabled(false);
    m_writeNotifier->setEnabled(false);
    m_readNotifier->deleteLater();
    m_writeNotifier->deleteLater();
    m_readNotifier = nullptr;
    m_writeNotifier = nullptr;

    // Closing the master already HUPs the session's foreground jobs; the
    // explicit SIGHUP covers a shell that's idle at its prompt.
    if (m_pid > 0)
        kill(m_pid, SIGHUP);
    ::close(m_masterFd);
    m_masterFd = -1;
    m_pending.clear();

    if (m_pid > 0)
        reapInBackground(m_pid);
    m_pid = -1;
}
