#pragma once

#include <optional>

#include <QByteArray>
#include <QString>
#include <QVector>

#include "ConnectionProfile.h"

// A tab's pane tree, as either a lone connection (Leaf) or a QSplitter with
// its own children (Splitter) - mirrors the QSplitter-tree shape MainWindow
// actually builds (see MainWindow::captureNode()/buildNode()). A Leaf's
// `connection` doubles as "how to reconnect this pane": the same
// ConnectionProfile JSON (and DPAPI-secret-encryption) already used for
// saved connections covers ad-hoc ones here too - see
// TerminalView::sessionSnapshot().
struct SessionNode
{
    enum class Kind
    {
        Leaf,
        Splitter,
    };

    Kind kind = Kind::Leaf;

    std::optional<ConnectionProfile> connection; // Leaf only

    Qt::Orientation orientation = Qt::Horizontal; // Splitter only
    QList<int> sizes; // Splitter only
    QVector<SessionNode> children; // Splitter only
};

struct SessionTab
{
    QString title;
    SessionNode root;
};

struct SessionState
{
    QByteArray windowGeometry; // QMainWindow::saveGeometry()
    QByteArray windowState; // QMainWindow::saveState() (dock layout)
    QVector<SessionTab> tabs;
    int currentTabIndex = 0;
};

// Persists the tab/pane layout and every pane's connection info (including
// ad-hoc SSH passwords, DPAPI-encrypted the same way saved profiles are) so
// the app reopens exactly where it left off. Loaded once at startup and
// saved once on close (see MainWindow) - no live "changed" signal needed,
// unlike ProfileStore.
namespace SessionStore
{
// Returns a default-constructed (empty tabs) SessionState if there's no
// session file yet, or it can't be read - callers should treat that as
// "nothing to restore", not an error.
SessionState load();
void save(const SessionState &state);
} // namespace SessionStore
