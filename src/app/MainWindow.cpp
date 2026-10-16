#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QPointer>
#include <QShowEvent>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QProxyStyle>
#include <QTabWidget>
#include <QTimer>

#include "core/ProfileStore.h"
#include "core/StateIcon.h"
#include "terminal/TerminalView.h"
#include "transports/loopback/LoopbackTransport.h"
#include "transports/raw/RawTransport.h"
#include "transports/serial/SerialTransport.h"
#include "transports/local/LocalTransport.h"
#include "transports/ssh/SshTransport.h"
#include "transports/telnet/TelnetTransport.h"
#include "ui/AboutDialog.h"
#include "ui/ConnectionTreeDock.h"
#include "ui/SerialConnectDialog.h"
#include "ui/SettingsDialog.h"
#include "core/AppSettings.h"
#include "ui/PaneSplitter.h"
#include "ui/SftpDock.h"
#include "ui/SftpFileEditor.h"
#include "ui/SplitPicker.h"
#include "ui/SshConnectDialog.h"
#include "ui/SshCredentialDialog.h"
#include "ui/TelnetConnectDialog.h"
#include "ui/WelcomeWidget.h"

namespace
{
// The base style, minus the frame QTabWidget draws around its page (and
// the margin that frame reserves).
class NoFrameTabStyle : public QProxyStyle
{
public:
    explicit NoFrameTabStyle(QObject *owner) : QProxyStyle(QStringLiteral("Fusion")) { setParent(owner); }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override
    {
        if (element == PE_FrameTabWidget)
            return;
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    QRect subElementRect(SubElement element, const QStyleOption *option, const QWidget *widget) const override
    {
        // The page gets the whole pane rect, not the pane rect shrunk by the
        // frame - except at the top: the selected tab is drawn a couple of
        // pixels over the pane's top edge, which would cover the top of
        // the active pane's border, so the page starts below that overlap.
        if (element == SE_TabWidgetTabContents) {
            QRect page = QProxyStyle::subElementRect(SE_TabWidgetTabPane, option, widget);
            const QRect framed = QProxyStyle::subElementRect(SE_TabWidgetTabContents, option, widget);
            if (const auto *tabs = qobject_cast<const QTabWidget *>(widget); !tabs || tabs->tabPosition() == QTabWidget::North)
                page.setTop(framed.top());
            else
                return framed;
            return page;
        }
        return QProxyStyle::subElementRect(element, option, widget);
    }
};
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_profileStore(new ProfileStore(this))
    , m_connectionDock(new ConnectionTreeDock(m_profileStore, this))
    , m_sftpDock(new SftpDock(this))
    , m_tabs(new QTabWidget(this))
{
    setWindowTitle(QStringLiteral("lvdterm"));
    resize(1000, 650);

    // No frame around the page area: the panes draw their own borders, and
    // this one would show as an extra outline around (and, with a small
    // gap, between) them. Set on the tab widget alone - a widget's style
    // isn't inherited by its children, so the tab bar looks as before.
    auto *tabStyle = new NoFrameTabStyle(m_tabs);
    m_tabs->setStyle(tabStyle);

    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::onCurrentTabChanged);
    connect(m_tabs->tabBar(), &QTabBar::tabBarDoubleClicked, this, &MainWindow::renameTab);
    m_tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabs->tabBar(), &QTabBar::customContextMenuRequested, this, &MainWindow::showTabContextMenu);
    setCentralWidget(m_tabs);

    addDockWidget(Qt::LeftDockWidgetArea, m_connectionDock);
    connect(m_connectionDock, &ConnectionTreeDock::connectionActivated, this, &MainWindow::openProfile);

    addDockWidget(Qt::RightDockWidgetArea, m_sftpDock);

    // The docks' natural minimums (~140 px: the widest button/column) add
    // up with the central area's to a window too wide to snap to half a
    // screen. Let them squeeze further - contents just clip.
    m_connectionDock->setMinimumWidth(100);
    m_sftpDock->setMinimumWidth(100);

    connect(qApp, &QApplication::focusChanged, this, &MainWindow::onFocusChanged);

    createMenus();

    const SessionState session = SessionStore::load();
    if (!session.tabs.isEmpty())
        restoreSession(session);
    else
        m_tabs->addTab(new WelcomeWidget(this), QStringLiteral("Welcome"));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Open SFTP file editors with unsaved changes get asked first (they're
    // separate windows owned by this one, so they'd otherwise just vanish
    // with it). Cancelling any of them cancels quitting.
    for (SftpFileEditor *editor : findChildren<SftpFileEditor *>()) {
        if (!editor->requestClose()) {
            event->ignore();
            return;
        }
    }

    SessionStore::save(captureSession());
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    // Only the very first real show matters - a restored session's
    // non-current tabs only ever need fixing up once. showEvent() fires
    // again on every subsequent show (e.g. un-minimizing), which would be
    // harmless to repeat but pointless.
    if (!m_firstShowHandled) {
        m_firstShowHandled = true;
        // Deferred one more event-loop turn: showEvent() fires as part of
        // the window becoming visible, but the window manager's own
        // frame/DPI/placement adjustments can still land as a follow-up
        // resize shortly after - queuing this rather than running it
        // inline gives that a chance to settle first, so
        // currentWidget()->size() (fixHiddenTabSizes()'s reference size)
        // reflects the true final geometry, not a provisional one.
        QTimer::singleShot(0, this, &MainWindow::fixHiddenTabSizes);
    }
}

void MainWindow::createMenus()
{
    QMenu *connectionMenu = menuBar()->addMenu(QStringLiteral("&Connection"));

    m_newLocalAction = connectionMenu->addAction(QStringLiteral("New &Local Terminal"));
    m_newLocalAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+T")));
    connect(m_newLocalAction, &QAction::triggered, this, &MainWindow::newLocalTerminal);

    connectionMenu->addSeparator();

    QAction *serialAction = connectionMenu->addAction(QStringLiteral("New &Serial Connection..."));
    connect(serialAction, &QAction::triggered, this, &MainWindow::newSerialConnection);

    QAction *telnetAction = connectionMenu->addAction(QStringLiteral("New &Telnet Connection..."));
    connect(telnetAction, &QAction::triggered, this, &MainWindow::newTelnetConnection);

    QAction *sshAction = connectionMenu->addAction(QStringLiteral("New SS&H Connection..."));
    connect(sshAction, &QAction::triggered, this, &MainWindow::newSshConnection);

    connectionMenu->addSeparator();

    QAction *loopbackAction = connectionMenu->addAction(QStringLiteral("New &Loopback (test)"));
    connect(loopbackAction, &QAction::triggered, this, &MainWindow::addLoopbackTab);

    connectionMenu->addSeparator();

    m_closeTabAction = connectionMenu->addAction(QStringLiteral("&Close Pane / Tab"));
    m_closeTabAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+W")));
    connect(m_closeTabAction, &QAction::triggered, this, &MainWindow::confirmCloseActivePane);

    // No "Pane" menu - these actions live only on `this` (so their
    // shortcuts keep working) and the tab bar's right-click context menu
    // (see showTabContextMenu()), per the plan's replace-not-supplement
    // decision.
    m_splitRightAction = new QAction(QStringLiteral("Split &Right"), this);
    m_splitRightAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
    connect(m_splitRightAction, &QAction::triggered, this, [this] { splitActivePane(Qt::Horizontal); });
    addAction(m_splitRightAction);

    m_splitDownAction = new QAction(QStringLiteral("Split &Down"), this);
    m_splitDownAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    connect(m_splitDownAction, &QAction::triggered, this, [this] { splitActivePane(Qt::Vertical); });
    addAction(m_splitDownAction);

    m_closePaneAction = new QAction(QStringLiteral("&Close Pane"), this);
    m_closePaneAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+W")));
    connect(m_closePaneAction, &QAction::triggered, this, &MainWindow::closeActivePane);
    addAction(m_closePaneAction);

    m_findAction = new QAction(QStringLiteral("&Find in Scrollback..."), this);
    m_findAction->setShortcut(QKeySequence::Find);
    connect(m_findAction, &QAction::triggered, this, [this] {
        if (TerminalView *view = currentActiveView())
            view->showFindBar();
    });
    addAction(m_findAction);

    m_logAction = new QAction(QStringLiteral("&Log Session to File..."), this);
    connect(m_logAction, &QAction::triggered, this, &MainWindow::toggleSessionLogging);
    addAction(m_logAction);

    // Unlike the actions above (kept off a menu deliberately, see the
    // comment there), this one *is* a real menu per your request - greyed
    // out except while a terminal pane has focus (see onFocusChanged()).
    QMenu *terminalMenu = menuBar()->addMenu(QStringLiteral("Te&rminal"));
    m_terminalMenuAction = terminalMenu->menuAction();
    m_terminalMenuAction->setEnabled(false);

    QAction *clearBufferAction = terminalMenu->addAction(QStringLiteral("Clear &Buffer"));
    connect(clearBufferAction, &QAction::triggered, this, [this] {
        if (TerminalView *view = currentActiveView())
            view->clearBuffer();
    });

    QAction *clearScreenAction = terminalMenu->addAction(QStringLiteral("Clear &Screen"));
    connect(clearScreenAction, &QAction::triggered, this, [this] {
        if (TerminalView *view = currentActiveView())
            view->clearScreen();
    });

    terminalMenu->addSeparator();

    QAction *copyAction = terminalMenu->addAction(QStringLiteral("&Copy"));
    connect(copyAction, &QAction::triggered, this, [this] {
        if (TerminalView *view = currentActiveView())
            view->copySelection();
    });

    QAction *pasteAction = terminalMenu->addAction(QStringLiteral("&Paste"));
    connect(pasteAction, &QAction::triggered, this, [this] {
        if (TerminalView *view = currentActiveView())
            view->pasteFromClipboard();
    });

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(m_connectionDock->toggleViewAction());
    viewMenu->addAction(m_sftpDock->toggleViewAction());

    // A menu-bar entry of its own that opens the dialog directly (no
    // dropdown with a single item in it).
    QAction *settingsAction = menuBar()->addAction(QStringLiteral("&Settings"));
    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog dialog(this);
        dialog.exec();
    });

    // The gap between split panes is a property of every splitter in
    // every tab - resize them all live as the setting changes.
    connect(&AppSettings::instance(), &AppSettings::paneAppearanceChanged, this, [this] {
        // (PaneSplitter has no Q_OBJECT - header-only, no signals - so
        // findChildren<PaneSplitter *> won't compile; filter by type.)
        for (QSplitter *splitter : findChildren<QSplitter *>()) {
            if (dynamic_cast<PaneSplitter *>(splitter))
                splitter->setHandleWidth(AppSettings::instance().paneGap());
        }
    });

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    QAction *aboutAction = helpMenu->addAction(QStringLiteral("&About lvdterm..."));
    connect(aboutAction, &QAction::triggered, this, [this] {
        AboutDialog dialog(this);
        dialog.exec();
    });
}

void MainWindow::toggleSessionLogging()
{
    TerminalView *view = currentActiveView();
    if (!view)
        return;

    if (view->isLogging()) {
        view->stopLogging();
        statusBar()->showMessage(QStringLiteral("Logging stopped"), 3000);
        return;
    }

    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Log Session to File"));
    if (path.isEmpty())
        return;

    view->startLogging(path);
    statusBar()->showMessage(view->isLogging() ? QStringLiteral("Logging to %1").arg(path) : QStringLiteral("Could not open %1 for logging").arg(path),
                              5000);
}

std::optional<MainWindow::NewConnectionResult> MainWindow::localResult(const QString &shell)
{
    ConnectionProfile snapshot;
    snapshot.type = ConnectionProfile::Type::Local;
    snapshot.name = QStringLiteral("Local");
    snapshot.localShell = shell;

    // Not auto-reconnected: the shell exiting (the user typed `exit`) is
    // the normal way a local session ends, not a dropped link to retry.
    NewConnectionResult result{new LocalTransport(shell), snapshot.name, [shell] { return new LocalTransport(shell); }, false};
    result.snapshot = snapshot;
    return result;
}

void MainWindow::newLocalTerminal()
{
    if (auto result = localResult(QString()))
        addTerminalTab(*result);
}

void MainWindow::addLoopbackTab()
{
    addTerminalTab({new LoopbackTransport(), QStringLiteral("loopback"), [] { return new LoopbackTransport(); }});
}

std::optional<MainWindow::NewConnectionResult> MainWindow::runSerialDialog()
{
    SerialConnectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;

    const SerialPortSettings settings = dialog.settings();
    if (settings.portName.isEmpty())
        return std::nullopt;

    ConnectionProfile snapshot;
    snapshot.type = ConnectionProfile::Type::Serial;
    snapshot.name = settings.portName;
    snapshot.serial = settings;

    NewConnectionResult result{new SerialTransport(settings), settings.portName, [settings] { return new SerialTransport(settings); }};
    result.snapshot = snapshot;
    return result;
}

std::optional<MainWindow::NewConnectionResult> MainWindow::runTelnetDialog()
{
    TelnetConnectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;

    const QString host = dialog.host();
    if (host.isEmpty())
        return std::nullopt;

    const quint16 port = dialog.port();
    const bool rawMode = dialog.rawMode();
    const QString title = QStringLiteral("%1:%2").arg(host).arg(port);

    ConnectionProfile snapshot;
    snapshot.type = ConnectionProfile::Type::Telnet;
    snapshot.name = title;
    snapshot.telnetHost = host;
    snapshot.telnetPort = port;
    snapshot.rawMode = rawMode;

    auto factory = [host, port, rawMode]() -> Transport * {
        if (rawMode)
            return new RawTransport(host, port);
        return new TelnetTransport(host, port);
    };
    NewConnectionResult result{factory(), title, factory};
    result.snapshot = snapshot;
    return result;
}

std::optional<MainWindow::NewConnectionResult> MainWindow::runSshDialog()
{
    SshConnectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;

    SshConnectionSettings settings = dialog.settings();
    if (settings.host.isEmpty() || (settings.username.isEmpty() && !settings.promptForUsername))
        return std::nullopt;

    // Resolve "ask each time" once, here, before anything is built from
    // settings - the interactive pane (SshTransport, below) and the SFTP
    // dock's own independent connection to the same host (SftpSession,
    // built later from result.sshSettings) must not each separately
    // prompt for the one thing the user is about to type once.
    if (!SshCredentialDialog::resolve(settings))
        return std::nullopt;

    const QString title = QStringLiteral("%1@%2").arg(settings.username, settings.host);

    ConnectionProfile snapshot;
    snapshot.type = ConnectionProfile::Type::Ssh;
    snapshot.name = title;
    snapshot.ssh = settings;

    NewConnectionResult result{new SshTransport(settings), title, [settings] { return new SshTransport(settings); }};
    result.sshSettings = settings;
    result.snapshot = snapshot;
    return result;
}

std::optional<MainWindow::NewConnectionResult> MainWindow::resultForProfile(const ConnectionProfile &profile)
{
    switch (profile.type) {
    case ConnectionProfile::Type::Serial: {
        const SerialPortSettings s = profile.serial;
        NewConnectionResult result{new SerialTransport(s), profile.name, [s] { return new SerialTransport(s); }, profile.autoReconnect};
        result.snapshot = profile;
        return result;
    }
    case ConnectionProfile::Type::Telnet: {
        const QString host = profile.telnetHost;
        const quint16 port = profile.telnetPort;
        const bool rawMode = profile.rawMode;
        auto factory = [host, port, rawMode]() -> Transport * {
            if (rawMode)
                return new RawTransport(host, port);
            return new TelnetTransport(host, port);
        };
        NewConnectionResult result{factory(), profile.name, factory, profile.autoReconnect};
        result.snapshot = profile;
        return result;
    }
    case ConnectionProfile::Type::Ssh: {
        SshConnectionSettings s = profile.ssh;
        // Resolve "ask each time" once, here, before anything is built
        // from s - see the identical comment in runSshDialog(). Applies
        // to every saved-profile connect path (open from the tree,
        // split-picker choice, session restore on launch), not just the
        // ad hoc quick-connect dialog.
        if (!SshCredentialDialog::resolve(s))
            return std::nullopt;
        NewConnectionResult result{new SshTransport(s), profile.name, [s] { return new SshTransport(s); }, profile.autoReconnect};
        result.sshSettings = s;
        result.snapshot = profile;
        return result;
    }
    case ConnectionProfile::Type::Local: {
        auto result = localResult(profile.localShell);
        result->title = profile.name;
        result->snapshot = profile;
        return result;
    }
    }
    Q_UNREACHABLE();
}

void MainWindow::newSerialConnection()
{
    if (auto result = runSerialDialog())
        addTerminalTab(*result);
}

void MainWindow::newTelnetConnection()
{
    if (auto result = runTelnetDialog())
        addTerminalTab(*result);
}

void MainWindow::newSshConnection()
{
    if (auto result = runSshDialog())
        addTerminalTab(*result);
}

void MainWindow::openProfile(const ConnectionProfile &profile)
{
    if (auto result = resultForProfile(profile))
        addTerminalTab(*result);
}

TerminalView *MainWindow::makeView(const NewConnectionResult &result)
{
    auto *view = new TerminalView(result.transport, result.recreate, result.sshSettings, result.snapshot, nullptr);
    view->setAutoReconnectEnabled(result.autoReconnect);

    connect(view, &TerminalView::errorOccurred, this, [this, view](const QString &message) {
        const int index = m_tabs->indexOf(view);
        const QString title = index >= 0 ? m_tabs->tabText(index) : view->windowTitle();
        statusBar()->showMessage(QStringLiteral("%1: %2").arg(title, message), 5000);
    });
    connect(view, &TerminalView::contextMenuRequested, this, [this, view](const QPoint &globalPos) { showPaneContextMenu(view, globalPos); });
    // Deferred a turn: the drag that produced this drop is still running its
    // event loop on the source pane's stack.
    connect(view, &TerminalView::paneDropped, this, [this, view](TerminalView *source, PaneDropZone zone) {
        QTimer::singleShot(0, this, [this, source = QPointer<TerminalView>(source), target = QPointer<TerminalView>(view), zone] {
            if (source && target)
                movePane(source, target, zone);
        });
    });
    connect(view, &TerminalView::stateChanged, this, [this, view] {
        if (QWidget *root = tabRootOf(view))
            updateTabIcon(root);
    });

    return view;
}

void MainWindow::showPaneContextMenu(TerminalView *view, const QPoint &globalPos)
{
    // Split/Close Pane act on the active pane - make it the one that was
    // actually right-clicked (focus may not have followed the click yet).
    m_activeView = view;
    view->setFocus();

    QWidget *root = tabRootOf(view);
    const bool multiplePanes = root && collectViews(root).size() > 1;

    QMenu menu(this);

    if (!view->isHexMode()) {
        connect(menu.addAction(QStringLiteral("Copy")), &QAction::triggered, view, &TerminalView::copySelection);
        connect(menu.addAction(QStringLiteral("Paste")), &QAction::triggered, view, &TerminalView::pasteFromClipboard);
        menu.addSeparator();
    }

    // Both rebuild this pane's Transport, which isn't safe mid-connect (see
    // TerminalView::doReconnectNow()), so they're greyed out meanwhile.
    QAction *respawnAction = menu.addAction(QStringLiteral("Respawn Terminal"));
    respawnAction->setEnabled(view->canRespawn());
    // Confirmed first: it closes the live connection and wipes the terminal's output.
    // Deferred a turn: the dialog is modal and shouldn't run inside this menu's exec().
    connect(respawnAction, &QAction::triggered, this, [this, view = QPointer<TerminalView>(view)] {
        QTimer::singleShot(0, this, [this, view] {
            if (!view)
                return;
            QMessageBox box(QMessageBox::Question, QStringLiteral("Respawn Terminal"),
                            QStringLiteral("Respawn this terminal?\n\nThe current connection is closed, a new one is started "
                                           "with the same settings, and the terminal's current output is cleared."),
                            QMessageBox::NoButton, this);
            QPushButton *confirm = box.addButton(QStringLiteral("Respawn"), QMessageBox::AcceptRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() == confirm && view)
                view->respawn();
        });
    });

    // Loopback has no snapshot (and no settings to change).
    QAction *settingsAction = menu.addAction(QStringLiteral("Change Settings..."));
    settingsAction->setEnabled(view->canRespawn() && view->sessionSnapshot().has_value());
    // Deferred a turn: the dialog is modal and shouldn't run inside this menu's exec().
    connect(settingsAction, &QAction::triggered, this,
            [this, view = QPointer<TerminalView>(view)] { QTimer::singleShot(0, this, [this, view] { if (view) changePaneSettings(view); }); });

    menu.addSeparator();

    // "\t<keys>" is just a right-aligned shortcut hint: the real
    // shortcuts live on m_splitRightAction/m_splitDownAction.
    connect(menu.addAction(QStringLiteral("Split Horizontally\tCtrl+Shift+R")), &QAction::triggered, this,
            [this] { splitActivePane(Qt::Horizontal); });
    connect(menu.addAction(QStringLiteral("Split Vertically\tCtrl+Shift+D")), &QAction::triggered, this, [this] { splitActivePane(Qt::Vertical); });

    menu.addSeparator();

    // Deferred a turn: closing deletes `view`, whose own right-click
    // handler is still on the stack underneath this menu's exec().
    connect(menu.addAction(multiplePanes ? QStringLiteral("Close Pane") : QStringLiteral("Close Tab")), &QAction::triggered, this,
            [this, view] { QTimer::singleShot(0, this, [this, view = QPointer<TerminalView>(view)] { if (view) confirmClosePane(view); }); });

    menu.exec(globalPos);
}

void MainWindow::changePaneSettings(TerminalView *view)
{
    if (!view->sessionSnapshot() || !view->canRespawn())
        return;

    // Only the protocol fields are edited; everything else about the pane
    // (keyboard profile, logging, viewer, name) carries over. This changes
    // the running pane only - a saved profile it was opened from is untouched.
    ConnectionProfile profile = *view->sessionSnapshot();
    const QString oldAutoName = profile.name;

    switch (profile.type) {
    case ConnectionProfile::Type::Serial: {
        SerialConnectDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Change Serial Settings"));
        dialog.setSettings(profile.serial);
        if (dialog.exec() != QDialog::Accepted || dialog.settings().portName.isEmpty())
            return;
        profile.serial = dialog.settings();
        break;
    }
    case ConnectionProfile::Type::Telnet: {
        TelnetConnectDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Change Telnet Settings"));
        dialog.setHost(profile.telnetHost);
        dialog.setPort(profile.telnetPort);
        dialog.setRawMode(profile.rawMode);
        if (dialog.exec() != QDialog::Accepted || dialog.host().isEmpty())
            return;
        profile.telnetHost = dialog.host();
        profile.telnetPort = dialog.port();
        profile.rawMode = dialog.rawMode();
        break;
    }
    case ConnectionProfile::Type::Ssh: {
        SshConnectDialog dialog(this);
        dialog.setWindowTitle(QStringLiteral("Change SSH Settings"));
        dialog.setSettings(profile.ssh);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const SshConnectionSettings s = dialog.settings();
        if (s.host.isEmpty() || (s.username.isEmpty() && !s.promptForUsername))
            return;
        profile.ssh = s;
        break;
    }
    case ConnectionProfile::Type::Local: {
        bool ok = false;
        const QString shell = QInputDialog::getText(this, QStringLiteral("Change Local Shell"),
                                                    QStringLiteral("Shell program (empty = platform default):"), QLineEdit::Normal,
                                                    profile.localShell, &ok);
        if (!ok)
            return;
        profile.localShell = shell.trimmed();
        break;
    }
    }

    // Keep an auto-generated name (user@host, COM3, host:port) in step with
    // the new settings; a name the user chose (saved profile, renamed tab)
    // is left alone.
    QString newAutoName;
    switch (profile.type) {
    case ConnectionProfile::Type::Serial: newAutoName = profile.serial.portName; break;
    case ConnectionProfile::Type::Telnet: newAutoName = QStringLiteral("%1:%2").arg(profile.telnetHost).arg(profile.telnetPort); break;
    case ConnectionProfile::Type::Ssh: newAutoName = QStringLiteral("%1@%2").arg(profile.ssh.username, profile.ssh.host); break;
    case ConnectionProfile::Type::Local: break;
    }
    const int tabIndex = m_tabs->indexOf(view);
    const bool nameWasAuto = tabIndex >= 0 && m_tabs->tabText(tabIndex) == oldAutoName;
    if (nameWasAuto && !newAutoName.isEmpty())
        profile.name = newAutoName;

    // Also resolves SSH "ask each time" credentials.
    auto result = resultForProfile(profile);
    if (!result)
        return;

    if (!view->reconfigure(result->transport, result->recreate, result->sshSettings, result->snapshot)) {
        delete result->transport;
        return;
    }

    if (nameWasAuto && !newAutoName.isEmpty())
        m_tabs->setTabText(tabIndex, newAutoName);

    if (view == m_activeView)
        m_sftpDock->setActiveConnection(view->sshSettings());
    if (QWidget *root = tabRootOf(view))
        updateTabIcon(root);
}

QWidget *MainWindow::tabRootOf(QWidget *w) const
{
    while (w && m_tabs->indexOf(w) < 0)
        w = w->parentWidget();
    return w;
}

void MainWindow::addTerminalTab(const NewConnectionResult &result)
{
    TerminalView *view = makeView(result);

    const int index = m_tabs->addTab(view, result.title);
    m_tabs->setCurrentIndex(index);
    view->connectToHost();
    view->setFocus();
    updateTabIcon(view);
    updatePaneFrames();
}

void MainWindow::onFocusChanged(QWidget * /*old*/, QWidget *now)
{
    TerminalView *focusedView = nullptr;
    for (QWidget *w = now; w; w = w->parentWidget()) {
        if (auto *view = qobject_cast<TerminalView *>(w)) {
            focusedView = view;
            break;
        }
    }

    // m_terminalMenuAction reflects real, current focus (cleared the
    // instant focus leaves every TerminalView) - unlike m_activeView below,
    // which is deliberately sticky (see its declaration) so Split/Close
    // Pane/the SFTP dock keep acting on the last-focused pane even while,
    // say, the SFTP dock itself now has focus.
    if (m_terminalMenuAction)
        m_terminalMenuAction->setEnabled(focusedView != nullptr);

    if (focusedView) {
        m_activeView = focusedView;
        m_sftpDock->setActiveConnection(focusedView->sshSettings());
        updatePaneFrames();
    }
}

void MainWindow::updatePaneFrames()
{
    // The accent border follows the sticky "active" pane (see
    // m_activeView), not literal focus - it stays on the last-used pane
    // while focus is in the SFTP dock or a dialog.
    TerminalView *active = currentActiveView();
    for (int i = 0; i < m_tabs->count(); ++i) {
        for (TerminalView *view : collectViews(m_tabs->widget(i)))
            view->setPaneActive(view == active);
    }
}

TerminalView *MainWindow::currentActiveView() const
{
    if (m_activeView)
        return m_activeView;

    if (QWidget *current = m_tabs->currentWidget()) {
        const QVector<TerminalView *> views = collectViews(current);
        if (!views.isEmpty())
            return views.first();
    }

    return nullptr;
}

void MainWindow::movePane(TerminalView *source, TerminalView *target, PaneDropZone zone)
{
    // Panes only move within their own tab (only one tab is ever on screen
    // while dragging), and both must sit in a splitter - which any tab with
    // two panes guarantees.
    auto *sourceSplitter = qobject_cast<QSplitter *>(source->parentWidget());
    auto *targetSplitter = qobject_cast<QSplitter *>(target->parentWidget());
    if (source == target || !sourceSplitter || !targetSplitter || tabRootOf(source) != tabRootOf(target))
        return;

    if (zone == PaneDropZone::Swap) {
        // A placeholder keeps the first slot open while the second one is
        // filled; replaceWidget() keeps each slot's size.
        auto *placeholder = new QWidget;
        sourceSplitter->replaceWidget(sourceSplitter->indexOf(source), placeholder);
        targetSplitter->replaceWidget(targetSplitter->indexOf(target), source);
        sourceSplitter->replaceWidget(sourceSplitter->indexOf(placeholder), target);
        delete placeholder;
        source->show(); // replaceWidget() reparents, which hides - see splitActivePane()
        target->show();
    } else {
        const Qt::Orientation orientation = (zone == PaneDropZone::Left || zone == PaneDropZone::Right) ? Qt::Horizontal : Qt::Vertical;
        const bool after = zone == PaneDropZone::Right || zone == PaneDropZone::Bottom;

        // Out of the old place first. That can collapse a splitter that
        // held only these two, which re-parents `target` - so its parent is
        // looked up again afterwards.
        source->setParent(nullptr); // synchronously drops sourceSplitter's count
        collapseSplitterIfSingle(sourceSplitter);

        QWidget *parent = target->parentWidget();
        if (auto *splitter = qobject_cast<QSplitter *>(parent); splitter && splitter->orientation() == orientation) {
            // Same direction as the target's own splitter: join it rather than nest.
            const int index = splitter->indexOf(target);
            QList<int> sizes = splitter->sizes();
            const int half = sizes.value(index) / 2;
            splitter->insertWidget(after ? index + 1 : index, source);
            if (half > 0) {
                sizes[index] = sizes.value(index) - half;
                sizes.insert(after ? index + 1 : index, half);
                splitter->setSizes(sizes);
            }
        } else {
            auto *wrapper = new PaneSplitter(orientation);
            if (auto *parentSplitter = qobject_cast<QSplitter *>(parent)) {
                const int index = parentSplitter->indexOf(target);
                const QList<int> sizes = parentSplitter->sizes();
                wrapper->addWidget(target); // reparents target out of parentSplitter
                wrapper->insertWidget(after ? 1 : 0, source);
                parentSplitter->insertWidget(index, wrapper);
                parentSplitter->setSizes(sizes);
            } else {
                const int tabIndex = m_tabs->indexOf(target);
                const QString tabTitle = m_tabs->tabText(tabIndex);
                const QIcon tabIcon = m_tabs->tabIcon(tabIndex);
                m_tabs->removeTab(tabIndex);
                wrapper->addWidget(target);
                wrapper->insertWidget(after ? 1 : 0, source);
                m_tabs->insertTab(tabIndex, wrapper, tabIcon, tabTitle);
                m_tabs->setCurrentIndex(tabIndex);
            }
            wrapper->show();
        }
        source->show();
        target->show();
    }

    m_activeView = source;
    source->setFocus();
    if (QWidget *root = tabRootOf(source))
        updateTabIcon(root);
    updatePaneFrames();
}

void MainWindow::splitActivePane(Qt::Orientation orientation)
{
    m_activeView = currentActiveView();
    if (!m_activeView) {
        statusBar()->showMessage(QStringLiteral("No pane to split - click into a terminal first"), 5000);
        return;
    }

    TerminalView *existing = m_activeView;
    auto *picker = new SplitPicker(m_profileStore);

    auto *splitter = new PaneSplitter(orientation);
    QWidget *parent = existing->parentWidget();

    if (auto *parentSplitter = qobject_cast<QSplitter *>(parent)) {
        const int idx = parentSplitter->indexOf(existing);
        splitter->addWidget(existing); // reparents existing out of parentSplitter
        splitter->addWidget(picker);
        parentSplitter->insertWidget(idx, splitter);
        if (QWidget *root = tabRootOf(splitter))
            updateTabIcon(root);
    } else {
        const int tabIndex = m_tabs->indexOf(existing);
        const QString tabTitle = m_tabs->tabText(tabIndex);
        const QIcon tabIcon = m_tabs->tabIcon(tabIndex);
        m_tabs->removeTab(tabIndex);
        splitter->addWidget(existing);
        splitter->addWidget(picker);
        m_tabs->insertTab(tabIndex, splitter, tabIcon, tabTitle);
        m_tabs->setCurrentIndex(tabIndex);
    }

    // QWidget::setParent() (which QSplitter::addWidget()/insertWidget()
    // call internally) hides a widget as a side effect of reparenting it,
    // even one that was already visible - Qt's docs say so explicitly.
    existing->show();
    picker->show();
    splitter->show();
    picker->setFocus();

    // The new pane starts as an inline, non-modal picker rather than a
    // stack of modal dialogs (QMessageBox -> type menu -> settings dialog,
    // the old flow) - see SplitPicker. It turns into a real TerminalView on
    // any choice, or collapses the split back out on cancel.
    connect(picker, &SplitPicker::cancelled, this, [this, splitter, picker, existing] {
        picker->setParent(nullptr); // synchronously drops splitter's count to 1
        collapseSplitterIfSingle(splitter);
        existing->setFocus();
        picker->deleteLater(); // not delete: this runs from picker's own signal
    });
    connect(picker, &SplitPicker::duplicateRequested, this, [this, splitter, picker, existing] {
        TransportFactory factory = existing->recreateFactory();
        finishSplitWithResult(splitter, picker,
                               NewConnectionResult{factory(), QString(), factory, existing->autoReconnectEnabled(), existing->sshSettings(),
                                                    existing->sessionSnapshot()});
    });
    connect(picker, &SplitPicker::profileChosen, this, [this, splitter, picker](const ConnectionProfile &profile) {
        if (auto result = resultForProfile(profile))
            finishSplitWithResult(splitter, picker, *result);
    });
    connect(picker, &SplitPicker::newSerialRequested, this, [this, splitter, picker] {
        if (auto result = runSerialDialog())
            finishSplitWithResult(splitter, picker, *result);
    });
    connect(picker, &SplitPicker::newTelnetRequested, this, [this, splitter, picker] {
        if (auto result = runTelnetDialog())
            finishSplitWithResult(splitter, picker, *result);
    });
    connect(picker, &SplitPicker::newLocalRequested, this, [this, splitter, picker] {
        if (auto result = localResult(QString()))
            finishSplitWithResult(splitter, picker, *result);
    });
    connect(picker, &SplitPicker::newSshRequested, this, [this, splitter, picker] {
        if (auto result = runSshDialog())
            finishSplitWithResult(splitter, picker, *result);
    });
}

void MainWindow::finishSplitWithResult(QSplitter *splitter, SplitPicker *picker, const NewConnectionResult &result)
{
    TerminalView *newView = makeView(result);
    splitter->replaceWidget(splitter->indexOf(picker), newView); // returns/unparents the old widget (picker)
    picker->deleteLater(); // not delete: this runs from picker's own signal

    newView->show(); // replaceWidget() reparents, which hides - see splitActivePane()
    if (QWidget *root = tabRootOf(splitter))
        updateTabIcon(root);

    newView->connectToHost();
    newView->setFocus();
    updatePaneFrames();
}

void MainWindow::collapseSplitterIfSingle(QSplitter *splitter)
{
    if (splitter->count() != 1)
        return;

    QWidget *root = tabRootOf(splitter);
    QWidget *remaining = splitter->widget(0);
    QWidget *grandParent = splitter->parentWidget();

    if (auto *grandSplitter = qobject_cast<QSplitter *>(grandParent)) {
        const int idx = grandSplitter->indexOf(splitter);
        remaining->setParent(nullptr);
        grandSplitter->insertWidget(idx, remaining);
    } else {
        const int tabIndex = m_tabs->indexOf(splitter);
        const QString tabTitle = m_tabs->tabText(tabIndex);
        const QIcon tabIcon = m_tabs->tabIcon(tabIndex);
        m_tabs->removeTab(tabIndex);
        remaining->setParent(nullptr);
        m_tabs->insertTab(tabIndex, remaining, tabIcon, tabTitle);
        m_tabs->setCurrentIndex(tabIndex);
        root = remaining; // the old root (splitter) is gone; the tab page is remaining now
    }
    delete splitter;
    remaining->show(); // setParent() above hides it - see the comment in splitActivePane()

    if (root)
        updateTabIcon(root);
}

SplitPicker *MainWindow::focusedPicker() const
{
    for (QWidget *w = QApplication::focusWidget(); w; w = w->parentWidget()) {
        if (auto *picker = qobject_cast<SplitPicker *>(w))
            return tabRootOf(picker) == m_tabs->currentWidget() ? picker : nullptr;
    }
    return nullptr;
}

void MainWindow::closeActivePane()
{
    // A pane that's still the "connect to:" picker (a split that hasn't
    // been given a connection yet) is a pane too - closing it means
    // cancelling it. Without this, the real pane next to it was closed
    // instead, leaving the picker alone in the tab.
    if (SplitPicker *picker = focusedPicker()) {
        emit picker->cancelled();
        return;
    }

    if (TerminalView *view = currentActiveView())
        closePane(view);
}

void MainWindow::closePane(TerminalView *view)
{
    // The tab's last real pane: closing it closes the tab, even if a
    // picker is still sitting beside it (a tab with no terminal in it
    // would just be an orphaned connection picker).
    if (QWidget *root = tabRootOf(view); root && collectViews(root).size() == 1) {
        closeTab(m_tabs->indexOf(root));
        return;
    }

    QWidget *parent = view->parentWidget();

    if (auto *splitter = qobject_cast<QSplitter *>(parent)) {
        m_activeView = nullptr;
        delete view;
        collapseSplitterIfSingle(splitter);
    } else {
        // This pane is the whole tab's content - closing it closes the tab.
        const int index = m_tabs->indexOf(view);
        if (index >= 0)
            closeTab(index);
        return;
    }

    if (auto *newFocusView = qobject_cast<TerminalView *>(m_tabs->currentWidget()))
        newFocusView->setFocus();
    updatePaneFrames();
}

void MainWindow::confirmCloseActivePane()
{
    QWidget *root = m_tabs->currentWidget();
    if (!root)
        return;

    // An unconfigured split's picker has no session to lose either - just
    // cancel it, no prompt.
    if (SplitPicker *picker = focusedPicker()) {
        emit picker->cancelled();
        return;
    }

    const QVector<TerminalView *> views = collectViews(root);

    // The welcome tab has no session to lose - nothing to confirm.
    if (views.isEmpty()) {
        closeTab(m_tabs->currentIndex());
        return;
    }

    // Just the pane that was last clicked/focused, not the whole tab -
    // m_activeView is sticky and may still point into another tab (or a
    // since-deleted pane's replacement), so only trust it if it's
    // actually in the current tab.
    confirmClosePane(views.contains(m_activeView) ? m_activeView : views.first());
}

void MainWindow::confirmClosePane(TerminalView *target)
{
    QWidget *root = tabRootOf(target);
    if (!root)
        return;

    const bool lastPane = collectViews(root).size() == 1;

    const QString question = lastPane ? QStringLiteral("Close \"%1\" and end its session?").arg(m_tabs->tabText(m_tabs->indexOf(root)))
                                      : QStringLiteral("Close this pane and end its session?");
    const QMessageBox::StandardButton answer = QMessageBox::question(this, lastPane ? QStringLiteral("Close Tab") : QStringLiteral("Close Pane"),
                                                                     question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    if (m_activeView == target)
        m_activeView = nullptr;
    closePane(target); // also closes the tab itself when this was its only pane
}

void MainWindow::closeTab(int index)
{
    QWidget *root = m_tabs->widget(index);
    if (!root)
        return;

    if (m_activeView && collectViews(root).contains(m_activeView))
        m_activeView = nullptr;

    m_tabs->removeTab(index);
    delete root; // cascades: destroys every TerminalView (and its Transport) under it
}

void MainWindow::onCurrentTabChanged(int index)
{
    QWidget *root = m_tabs->widget(index);
    if (!root)
        return;

    const QVector<TerminalView *> views = collectViews(root);
    if (!views.isEmpty())
        views.first()->setFocus();
    updatePaneFrames();
}

void MainWindow::renameTab(int index)
{
    if (index < 0)
        return;

    bool ok = false;
    const QString newName =
        QInputDialog::getText(this, QStringLiteral("Rename Tab"), QStringLiteral("Tab name:"), QLineEdit::Normal, m_tabs->tabText(index), &ok);
    if (ok && !newName.isEmpty())
        m_tabs->setTabText(index, newName);
}

void MainWindow::showTabContextMenu(const QPoint &pos)
{
    const int index = m_tabs->tabBar()->tabAt(pos);
    if (index < 0)
        return;

    // The pane actions (Split/Close Pane/Find/Log) act on currentActiveView()
    // - switch to the right-clicked tab first if it wasn't already current,
    // so they resolve against the correct pane.
    if (index != m_tabs->currentIndex())
        m_tabs->setCurrentIndex(index);

    QMenu menu(this);
    menu.addAction(m_splitRightAction);
    menu.addAction(m_splitDownAction);
    menu.addAction(m_closePaneAction);
    menu.addSeparator();
    menu.addAction(m_findAction);
    menu.addAction(m_logAction);
    menu.addSeparator();

    QAction *renameAction = menu.addAction(QStringLiteral("Rename Tab..."));
    connect(renameAction, &QAction::triggered, this, [this, index] { renameTab(index); });

    QAction *closeTabAction = menu.addAction(QStringLiteral("Close Tab"));
    connect(closeTabAction, &QAction::triggered, this, [this, index] { closeTab(index); });

    menu.exec(m_tabs->tabBar()->mapToGlobal(pos));
}

QVector<TerminalView *> MainWindow::collectViews(QWidget *root)
{
    QVector<TerminalView *> views;
    if (auto *view = qobject_cast<TerminalView *>(root)) {
        views.append(view);
    } else if (auto *splitter = qobject_cast<QSplitter *>(root)) {
        for (int i = 0; i < splitter->count(); ++i)
            views += collectViews(splitter->widget(i));
    }
    return views;
}

void MainWindow::updateTabIcon(QWidget *tabRoot)
{
    const int index = m_tabs->indexOf(tabRoot);
    if (index < 0)
        return;

    // Worst-first: a tab with any errored pane shows red even if its
    // other panes are fine, since that's the one that needs attention.
    Transport::State worst = Transport::State::Connected;
    bool any = false;
    for (TerminalView *view : collectViews(tabRoot)) {
        const Transport::State s = view->transportState();
        any = true;
        if (s == Transport::State::Error) {
            worst = s;
            break;
        }
        if (s == Transport::State::Connecting && worst != Transport::State::Error)
            worst = s;
        else if (s == Transport::State::Disconnected && worst == Transport::State::Connected)
            worst = s;
    }

    if (any)
        m_tabs->setTabIcon(index, StateIcon::icon(worst));
}

std::optional<SessionNode> MainWindow::captureNode(QWidget *widget) const
{
    if (auto *view = qobject_cast<TerminalView *>(widget)) {
        if (!view->sessionSnapshot())
            return std::nullopt; // loopback, or anything else never meant to persist

        SessionNode node;
        node.kind = SessionNode::Kind::Leaf;
        node.connection = view->sessionSnapshot();
        return node;
    }

    if (auto *splitter = qobject_cast<QSplitter *>(widget)) {
        SessionNode node;
        node.kind = SessionNode::Kind::Splitter;
        node.orientation = splitter->orientation();
        node.sizes = splitter->sizes();

        for (int i = 0; i < splitter->count(); ++i) {
            std::optional<SessionNode> child = captureNode(splitter->widget(i));
            if (!child)
                return std::nullopt; // any unpersistable child (e.g. a loopback pane mixed into a split) drops the whole tab -
                                      // simplest correct behavior rather than a partial/placeholder restore
            node.children.append(*child);
        }
        return node;
    }

    return std::nullopt; // WelcomeWidget, or a SplitPicker still mid-choice when the app closed
}

SessionState MainWindow::captureSession() const
{
    SessionState state;
    state.windowGeometry = saveGeometry();
    state.windowState = saveState();
    state.currentTabIndex = m_tabs->currentIndex();

    for (int i = 0; i < m_tabs->count(); ++i) {
        if (std::optional<SessionNode> node = captureNode(m_tabs->widget(i)))
            state.tabs.append(SessionTab{m_tabs->tabText(i), *node});
    }

    return state;
}

QWidget *MainWindow::buildNode(const SessionNode &node)
{
    if (node.kind == SessionNode::Kind::Leaf) {
        if (!node.connection)
            return nullptr;
        auto result = resultForProfile(*node.connection);
        return result ? makeView(*result) : nullptr;
    }

    auto *splitter = new PaneSplitter(node.orientation);
    for (const SessionNode &child : node.children) {
        if (QWidget *childWidget = buildNode(child))
            splitter->addWidget(childWidget);
    }

    if (splitter->count() == 0) {
        delete splitter;
        return nullptr;
    }
    if (!node.sizes.isEmpty())
        splitter->setSizes(node.sizes);
    return splitter;
}

void MainWindow::restoreSession(const SessionState &state)
{
    for (const SessionTab &tab : state.tabs) {
        if (QWidget *root = buildNode(tab.root))
            m_tabs->addTab(root, tab.title);
    }

    if (state.currentTabIndex >= 0 && state.currentTabIndex < m_tabs->count())
        m_tabs->setCurrentIndex(state.currentTabIndex);

    if (!state.windowGeometry.isEmpty())
        restoreGeometry(state.windowGeometry);
    if (!state.windowState.isEmpty())
        restoreState(state.windowState);

    // Every restored leaf connects only now that its whole tab tree exists,
    // so tabRootOf()/updateTabIcon() (driven by each connection's
    // stateChanged, see makeView()) see a fully-built tab rather than one
    // still under construction. Every restored pane's terminal size is
    // still whatever tiny placeholder it had right after construction at
    // this point (this runs from MainWindow's own constructor, before the
    // window has ever been shown - there's no real on-screen geometry to
    // read yet) - fixHiddenTabSizes(), called once from showEvent() after
    // the window is actually on screen, corrects every non-current tab.
    // The currently-current tab corrects itself the normal way, the same
    // as any freshly-shown widget.
    for (int i = 0; i < m_tabs->count(); ++i) {
        QWidget *root = m_tabs->widget(i);
        for (TerminalView *view : collectViews(root)) {
            view->connectToHost();
        }
        updateTabIcon(root);
    }
}

void MainWindow::fixHiddenTabSizes()
{
    QWidget *current = m_tabs->currentWidget();
    if (!current)
        return;

    const QSize targetSize = current->size();
    for (int i = 0; i < m_tabs->count(); ++i) {
        QWidget *root = m_tabs->widget(i);
        if (root != current)
            applyCorrectSize(root, targetSize);
    }
}

void MainWindow::applyCorrectSize(QWidget *widget, const QSize &size)
{
    if (auto *view = qobject_cast<TerminalView *>(widget)) {
        view->ensureCorrectSize(size);
        return;
    }

    if (auto *splitter = qobject_cast<QSplitter *>(widget)) {
        // Resizing the splitter itself first (rather than assuming its
        // hidden children's sizes are already correct and just reading
        // them) is what makes this reliable regardless of how deep the
        // hidden-tab-page geometry problem actually goes at each nesting
        // level - every widget in the chain gets an explicit resize()
        // call from code that's actually running, not an implicit cascade
        // from a distant hidden ancestor. QSplitter preserves each
        // child's relative proportion when resized, the same as it does
        // for an ordinary visible split.
        splitter->resize(size);
        for (int i = 0; i < splitter->count(); ++i)
            applyCorrectSize(splitter->widget(i), splitter->widget(i)->size());
    }
}
