#pragma once

#include <functional>
#include <optional>

#include <QHash>
#include <QList>
#include <QMainWindow>

#include "core/ConnectionProfile.h"
#include "core/SessionStore.h"
#include "core/Transport.h"
#include "terminal/PaneDropZone.h"
#include "transports/ssh/SshConnectionSettings.h"

class QAction;
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
    // `primary` is null for the application's main window. A non-null one
    // makes this a secondary window that tabs get detached into (see
    // detachTab()): it shares the primary's saved connections, starts with
    // its side docks hidden and no session of its own to restore.
    explicit MainWindow(MainWindow *primary = nullptr);
    ~MainWindow() override;

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

    // Keyboard shortcuts (see ShortcutManager): every command's key comes
    // from there and is re-applied live when the Keyboard Shortcuts page
    // changes one. `terminalOnly` commands are only enabled while a terminal
    // pane has keyboard focus, so their keys reach other widgets otherwise.
    void registerCommand(const QString &id, QAction *action, bool terminalOnly = false);
    void applyShortcuts();
    void rebuildCustomShortcuts(); // the user's "send text" shortcuts
    void setTerminalFocused(bool focused);
    void addLoopbackTab();
    void newSerialConnection();
    void newTelnetConnection();
    void newSshConnection();
    std::optional<NewConnectionResult> localResult(const QString &shell); // empty = the platform's default shell
    void newLocalTerminal();
    void openProfile(const ConnectionProfile &profile);

    std::optional<NewConnectionResult> runSerialDialog();
    std::optional<NewConnectionResult> runTelnetDialog();
    std::optional<NewConnectionResult> runSshDialog();
    std::optional<NewConnectionResult> resultForProfile(const ConnectionProfile &profile);

    void addTerminalTab(const NewConnectionResult &result);
    TerminalView *makeView(const NewConnectionResult &result);

    void showPaneContextMenu(TerminalView *view, const QPoint &globalPos); // right-click inside a pane
    void changePaneSettings(TerminalView *view); // "Change Settings...": edit port/baud/host/... and reconnect this pane

    void movePane(TerminalView *source, TerminalView *target, PaneDropZone zone); // drag and drop: dropped on `target`'s edge (split) or middle (swap)
    void splitActivePane(Qt::Orientation orientation);
    void finishSplitWithResult(QSplitter *splitter, SplitPicker *picker, const NewConnectionResult &result);
    void collapseSplitterIfSingle(QSplitter *splitter); // shared by closeActivePane() and a cancelled SplitPicker
    void closeActivePane();
    SplitPicker *focusedPicker() const; // the split picker holding keyboard focus in the current tab, if any
    void closeTab(int index);
    void closePane(TerminalView *view); // closes just this pane (the tab too if it was the last one) - no prompt
    void confirmClosePane(TerminalView *view); // closePane() after asking
    void confirmCloseActivePane(); // Ctrl+W - the active pane only (the tab too if it was the last one); asks first
    void renameTab(int index);
    void toggleSessionLogging();
    void onFocusChanged(QWidget *old, QWidget *now);
    void updatePaneFrames(); // accent border on the active pane only
    void showTabContextMenu(const QPoint &pos);

    // Detaching a tab: the tab's whole page (panes, live connections and
    // all) is re-homed into another MainWindow - nothing reconnects.
    void detachTab(int index);          // into a new secondary window
    void moveTabToPrimary(int index);   // back into the main window
    void transferTab(int index, MainWindow *dest);
    void acceptTab(QWidget *root, const QString &title, const QIcon &icon);
    void wireView(TerminalView *view);  // connects a pane's signals to this window
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

    static QList<MainWindow *> s_windows; // every open window, the primary first
    static bool s_quitting;               // the primary is closing: secondary windows go without asking
    bool m_secondary = false;

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
    QAction *m_closeTabAction = nullptr; // Ctrl+W - lives in the Connection menu
    QAction *m_newLocalAction = nullptr; // Ctrl+T - lives in the Connection menu

    // The "Terminal" menu (Clear Buffer/Clear Screen/Copy/Paste) - its own
    // QAction (as returned by menuBar()->addMenu()) is kept so
    // onFocusChanged() can grey the whole menu out except while a terminal
    // pane actually has focus, unlike m_activeView above which deliberately
    // stays sticky after focus moves elsewhere (see onFocusChanged()).
    QAction *m_terminalMenuAction = nullptr;

    QHash<QString, QAction *> m_commandActions; // ShortcutManager command id -> its action
    QList<QAction *> m_terminalOnlyActions; // enabled only while a terminal pane has focus
    QList<QAction *> m_customActions; // the current custom shortcuts' actions (also in the list above)
    bool m_terminalFocused = false;
};
