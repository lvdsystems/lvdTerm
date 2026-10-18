#include <libssh2.h>

#ifndef _WIN32
#include <csignal>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <QApplication>
#include <QByteArray>
#include <QDebug>
#include <QIcon>
#include <QStyleFactory>

#include "MainWindow.h"
#include "Theme.h"
#include "Version.h"
#include "core/AppSettings.h"
#include "core/ThreadReaper.h"
#include "terminal/KeyboardProfiles.h"
#include "transports/ssh/SshConnectionSettings.h"

#ifndef _WIN32
namespace
{
bool isSocket(const QByteArray &path)
{
    struct stat info;
    return stat(path.constData(), &info) == 0 && S_ISSOCK(info.st_mode);
}

void ensureSessionBusAddress()
{
    const QByteArray current = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    const QByteArray pathKey = "unix:path=";
    if (!current.isEmpty()) {
        if (!current.startsWith(pathKey))
            return;
        const QList<QByteArray> parts = current.split(',');
        const QByteArray path = parts.first().mid(pathKey.size());
        if (isSocket(path)) {
            QByteArrayList kept;
            for (const QByteArray &part : parts) {
                if (!part.startsWith("guid="))
                    kept << part;
            }
            if (kept.size() != parts.size())
                qputenv("DBUS_SESSION_BUS_ADDRESS", kept.join(','));
            return;
        }
    }

    QByteArray runtimeDir = qgetenv("XDG_RUNTIME_DIR");
    if (runtimeDir.isEmpty())
        runtimeDir = "/run/user/" + QByteArray::number(static_cast<qlonglong>(getuid()));

    const QByteArray socketPath = runtimeDir + "/bus";
    if (isSocket(socketPath))
        qputenv("DBUS_SESSION_BUS_ADDRESS", pathKey + socketPath);
}
} // namespace
#endif

int main(int argc, char *argv[])
{
#ifndef _WIN32
    // A write to a socket whose peer reset the connection raises SIGPIPE
    // on Linux, which kills the process by default - no Windows analog,
    // and not just lvdterm's own send() calls: libssh2's internal socket
    // I/O goes through the same fd, so a dropped SSH connection mid-
    // transfer could otherwise take the whole app down with it.
    signal(SIGPIPE, SIG_IGN);
    ensureSessionBusAddress(); // before QApplication reads it - see above
#endif

    QApplication app(argc, argv);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setApplicationName(QStringLiteral("lvdterm"));
    QApplication::setOrganizationName(QStringLiteral("lvdterm"));
    QApplication::setApplicationVersion(QStringLiteral(LVDTERM_VERSION_STRING));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app_icon.png")));

    Theme::apply(AppSettings::instance().theme());
    KeyboardProfiles::registerAll(); // before any TerminalSession exists - see KeyboardProfiles.h

    // Needed for SshTransport::promptForPassword's SshCredentialPrompt
    // return value to cross the worker-thread -> GUI-thread blocking
    // queued call (see SshWorker::start()'s retry-on-rejected-password
    // loop) - before any SSH connection can possibly be started.
    qRegisterMetaType<SshCredentialPrompt>("SshCredentialPrompt");

    if (libssh2_init(0) != 0)
        qWarning("libssh2_init() failed; SSH connections will not work");

    MainWindow window;
    window.show();

    const int result = QApplication::exec();

    // SshTransport/SftpSession hand still-shutting-down worker threads
    // off to ThreadReaper instead of blocking their own destructors (see
    // ThreadReaper.h) - closing every pane/dock above may have left a
    // few of those still running. libssh2_exit() isn't safe to call
    // concurrently with any in-flight libssh2 call, so this is the one
    // place that still waits, bounded, for stragglers before it runs.
    ThreadReaper::waitForAll(20000);

    libssh2_exit();
    return result;
}
