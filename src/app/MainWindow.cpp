#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QShowEvent>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>

#include "core/ProfileStore.h"
#include "core/StateIcon.h"
#include "terminal/TerminalView.h"
#include "transports/loopback/LoopbackTransport.h"
#include "transports/raw/RawTransport.h"
#include "transports/serial/SerialTransport.h"
#include "transports/ssh/SshTransport.h"
#include "transports/telnet/TelnetTransport.h"
#include "ui/AboutDialog.h"
#include "ui/ConnectionTreeDock.h"
#include "ui/SerialConnectDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/SftpDock.h"
#include "ui/SplitPicker.h"
#include "ui/SshConnectDialog.h"
#include "ui/TelnetConnectDialog.h"
#include "ui/WelcomeWidget.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_profileStore(new ProfileStore(this))
    , m_connectionDock(new ConnectionTreeDock(m_profileStore, this))
    , m_sftpDock(new SftpDock(this))
    , m_tabs(new QTabWidget(this))
{
    setWindowTitle(QStringLiteral("lvdterm"));
    resize(1000, 650);

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

    QAction *serialAction = connectionMenu->addAction(QStringLiteral("New &Serial Connection..."));
    connect(serialAction, &QAction::triggered, this, &MainWindow::newSerialConnection);

    QAction *telnetAction = connectionMenu->addAction(QStringLiteral("New &Telnet Connection..."));
    connect(telnetAction, &QAction::triggered, this, &MainWindow::newTelnetConnection);

    QAction *sshAction = connectionMenu->addAction(QStringLiteral("New SS&H Connection..."));
    connect(sshAction, &QAction::triggered, this, &MainWindow::newSshConnection);

    connectionMenu->addSeparator();

    QAction *loopbackAction = connectionMenu->addAction(QStringLiteral("New &Loopback (test)"));
    connect(loopbackAction, &QAction::triggered, this, &MainWindow::addLoopbackTab);

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
    viewMenu->addSeparator();

    QAction *settingsAction = viewMenu->addAction(QStringLiteral("&Settings..."));
    connect(settingsAction, &QAction::triggered, this, [this] {
        SettingsDialog dialog(this);
        dialog.exec();
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

    const SshConnectionSettings settings = dialog.settings();
    if (settings.host.isEmpty() || settings.username.isEmpty())
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

MainWindow::NewConnectionResult MainWindow::resultForProfile(const ConnectionProfile &profile)
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
        const SshConnectionSettings s = profile.ssh;
        NewConnectionResult result{new SshTransport(s), profile.name, [s] { return new SshTransport(s); }, profile.autoReconnect};
        result.sshSettings = s;
        result.snapshot = profile;
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
    addTerminalTab(resultForProfile(profile));
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
    connect(view, &TerminalView::stateChanged, this, [this, view] {
        if (QWidget *root = tabRootOf(view))
            updateTabIcon(root);
    });

    return view;
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

void MainWindow::splitActivePane(Qt::Orientation orientation)
{
    m_activeView = currentActiveView();
    if (!m_activeView) {
        statusBar()->showMessage(QStringLiteral("No pane to split - click into a terminal first"), 5000);
        return;
    }

    TerminalView *existing = m_activeView;
    auto *picker = new SplitPicker(m_profileStore);

    auto *splitter = new QSplitter(orientation);
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
        finishSplitWithResult(splitter, picker, resultForProfile(profile));
    });
    connect(picker, &SplitPicker::newSerialRequested, this, [this, splitter, picker] {
        if (auto result = runSerialDialog())
            finishSplitWithResult(splitter, picker, *result);
    });
    connect(picker, &SplitPicker::newTelnetRequested, this, [this, splitter, picker] {
        if (auto result = runTelnetDialog())
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

void MainWindow::closeActivePane()
{
    TerminalView *view = currentActiveView();
    if (!view)
        return;

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
        return makeView(resultForProfile(*node.connection));
    }

    auto *splitter = new QSplitter(node.orientation);
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
