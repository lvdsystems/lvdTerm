#include <libssh2.h>

#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QStyleFactory>

#include "MainWindow.h"
#include "Theme.h"
#include "Version.h"
#include "core/AppSettings.h"
#include "core/ThreadReaper.h"
#include "terminal/KeyboardProfiles.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setApplicationName(QStringLiteral("lvdterm"));
    QApplication::setOrganizationName(QStringLiteral("lvdterm"));
    QApplication::setApplicationVersion(QStringLiteral(LVDTERM_VERSION_STRING));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app_icon.png")));

    Theme::apply(AppSettings::instance().theme());
    KeyboardProfiles::registerAll(); // before any TerminalSession exists - see KeyboardProfiles.h

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
