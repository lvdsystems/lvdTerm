#pragma once

#include <functional>
#include <optional>

#include <QMainWindow>

#include "core/ConnectionProfile.h"
#include "core/SessionStore.h"
#include "core/Transport.h"
#include "transports/ssh/SshConnectionSettings.h"

class QCloseEvent;
class QShowEvent;

// A tab's content starts as a lone TerminalView and can grow into a
// recursive QSplitter tree of them (see splitActivePane()/closeActivePane()
// in MainWindow.cpp).
class QTabWidget;
class QSplitter;
class Transport;
class TerminalView;
class ProfileStore;
class ConnectionTreeDock;
class SftpDock;
class SplitPicker;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    // Builds a Transport the same way a quick-connect dialog would, but
    // also returns a factory that can build another one with the same
    // settings later - used both for "New X Connection..." and for
    // "duplicate this connection" when splitting a pane.
    using TransportFactory = std::function<Transport *()>;
    struct NewConnectionResult
    {
        Transport *transport;
        QString title;
        TransportFactory recreate;
        bool autoReconnect = true;
        std::optional<SshConnectionSettings> sshSettings; // feeds the SFTP dock, see TerminalView
        // "How to reconnect this pane" for session save/restore (see
        // SessionStore) - std::nullopt for panes that are never persisted
        // (loopback, the welcome tab).
        std::optional<ConnectionProfile> snapshot;
    };

    void createMenus();
    void addLoopbackTab();
    void newSerialConnection();
    void newTelnetConnection();
    void newSshConnection();
    void openProfile(const ConnectionProfile &profile);

    std::optional<NewConnectionResult> runSerialDialog();
    std::optional<NewConnectionResult> runTelnetDialog();
    std::optional<NewConnectionResult> runSshDialog();
    NewConnectionResult resultForProfile(const ConnectionProfile &profile);

    void addTerminalTab(const NewConnectionResult &result);
    TerminalView *makeView(const NewConnectionResult &result);

    void splitActivePane(Qt::Orientation orientation);
    void finishSplitWithResult(QSplitter *splitter, SplitPicker *picker, const NewConnectionResult &result);
    void collapseSplitterIfSingle(QSplitter *splitter); // shared by closeActivePane() and a cancelled SplitPicker
    void closeActivePane();
    void closeTab(int index);
    void renameTab(int index);
    void toggleSessionLogging();
    void onFocusChanged(QWidget *old, QWidget *now);
    void showTabContextMenu(const QPoint &pos);
    void onCurrentTabChanged(int index); // focuses the new tab's first pane

    // m_activeView is only set once a real Qt focus-change event has
    // fired for some pane; at startup (before the user has clicked into
    // anything) or right after opening a tab whose window isn't active
    // yet, that may not have happened. This falls back to the first pane
    // in the current tab so Split/Close Pane still have something to act
    // on rather than silently doing nothing.
    TerminalView *currentActiveView() const;

    QWidget *tabRootOf(QWidget *w) const; // walks up to the tab-page ancestor, or nullptr
    void updateTabIcon(QWidget *tabRoot);
    static QVector<TerminalView *> collectViews(QWidget *root);

    // Session save/restore (see SessionStore) - captureNode()/buildNode()
    // walk the same QSplitter-tree shape splitActivePane()/closeActivePane()
    // build, converting to/from SessionNode.
    SessionState captureSession() const;
    void restoreSession(const SessionState &state);
    std::optional<SessionNode> captureNode(QWidget *widget) const;
    QWidget *buildNode(const SessionNode &node);
    void fixHiddenTabSizes(); // see showEvent()
    static void applyCorrectSize(QWidget *widget, const QSize &size); // recursive: TerminalView leaf or QSplitter

    ProfileStore *m_profileStore = nullptr;
    ConnectionTreeDock *m_connectionDock = nullptr;
    SftpDock *m_sftpDock = nullptr;
    QTabWidget *m_tabs = nullptr;

    TerminalView *m_activeView = nullptr;
    bool m_firstShowHandled = false; // showEvent() fires on every show (e.g. un-minimizing), not just the first

    // Owned by `this` (added via addAction() rather than a menu, see
    // createMenus()) so their shortcuts keep working with no "Pane" menu to
    // host them - the tab context menu (showTabContextMenu()) reuses these
    // same QActions rather than building duplicates.
    QAction *m_splitRightAction = nullptr;
    QAction *m_splitDownAction = nullptr;
    QAction *m_closePaneAction = nullptr;
    QAction *m_findAction = nullptr;
    QAction *m_logAction = nullptr;

    // The "Terminal" menu (Clear Buffer/Clear Screen/Copy/Paste) - its own
    // QAction (as returned by menuBar()->addMenu()) is kept so
    // onFocusChanged() can grey the whole menu out except while a terminal
    // pane actually has focus, unlike m_activeView above which deliberately
    // stays sticky after focus moves elsewhere (see onFocusChanged()).
    QAction *m_terminalMenuAction = nullptr;
};
