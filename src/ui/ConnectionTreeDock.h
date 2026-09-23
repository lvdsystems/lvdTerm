#pragma once

#include <QDockWidget>
#include <QHash>

#include "core/ConnectionProfile.h"

class QTreeWidget;
class QTreeWidgetItem;
class ProfileStore;

// Dockable "Saved Connections" panel: a folder tree, nested arbitrarily
// deep via ConnectionProfile::folder's "/"-separated path (a folder node
// exists the moment any profile uses that path, or - for one created via
// "New Folder..." with nothing in it yet - via ProfileStore::
// emptyFolders(), see there) with add/edit/duplicate/delete, and
// double-click to connect. Does not own ProfileStore.
//
// Not implemented in this pass: drag-and-drop re-filing between folders.
// Move a connection to a different folder via Edit... for now.
class ConnectionTreeDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit ConnectionTreeDock(ProfileStore *store, QWidget *parent = nullptr);

signals:
    void connectionActivated(const ConnectionProfile &profile);

private slots:
    void rebuildTree();
    void onItemActivated(QTreeWidgetItem *item);
    void onContextMenuRequested(const QPoint &pos);
    void newConnection();
    void editItem(QTreeWidgetItem *item);
    void duplicateItem(QTreeWidgetItem *item);
    void deleteItem(QTreeWidgetItem *item);
    void renameFolderItem(QTreeWidgetItem *item);
    void newFolder(QTreeWidgetItem *item); // nests under `item` if it's a folder, else a sibling of it
    void deleteFolder(QTreeWidgetItem *item);

private:
    int countConnectionsUnder(const QString &folderPath) const; // includes subfolders - for the delete-folder confirmation
    enum Kind
    {
        FolderKind,
        ConnectionKind,
    };

    QTreeWidgetItem *ensureFolderItem(const QString &path, QHash<QString, QTreeWidgetItem *> &folderItems);
    QString folderPathOf(QTreeWidgetItem *item) const; // the folder a new item under `item` should land in
    Kind kindOf(QTreeWidgetItem *item) const;
    QUuid connectionIdOf(QTreeWidgetItem *item) const;

    ProfileStore *m_store = nullptr;
    QTreeWidget *m_tree = nullptr;
};
