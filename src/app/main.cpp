#include <libssh2.h>

#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QStyleFactory>

#include "MainWindow.h"
#include "Theme.h"
#include "Version.h"
#include "core/AppSettings.h"
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

    libssh2_exit();
    return result;
}
