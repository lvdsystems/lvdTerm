#include "ConnectionTreeDock.h"

#include <QApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QStyle>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ConnectionEditDialog.h"
#include "core/ProfileStore.h"

namespace
{
constexpr int kKindRole = Qt::UserRole;
constexpr int kPayloadRole = Qt::UserRole + 1; // folder path, or connection id string
} // namespace

ConnectionTreeDock::ConnectionTreeDock(ProfileStore *store, QWidget *parent)
    : QDockWidget(QStringLiteral("Saved Connections"), parent)
    , m_store(store)
    , m_tree(new QTreeWidget(this))
{
    setObjectName(QStringLiteral("ConnectionTreeDock"));

    m_tree->setHeaderHidden(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &ConnectionTreeDock::onContextMenuRequested);
    connect(m_tree, &QTreeWidget::itemActivated, this, &ConnectionTreeDock::onItemActivated);

    auto *toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16, 16));
    QAction *newAction = toolbar->addAction(QStringLiteral("New Connection..."));
    connect(newAction, &QAction::triggered, this, &ConnectionTreeDock::newConnection);

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar);
    layout->addWidget(m_tree);
    setWidget(container);

    connect(m_store, &ProfileStore::changed, this, &ConnectionTreeDock::rebuildTree);
    rebuildTree();
}

ConnectionTreeDock::Kind ConnectionTreeDock::kindOf(QTreeWidgetItem *item) const
{
    return static_cast<Kind>(item->data(0, kKindRole).toInt());
}

QUuid ConnectionTreeDock::connectionIdOf(QTreeWidgetItem *item) const
{
    return QUuid(item->data(0, kPayloadRole).toString());
}

QString ConnectionTreeDock::folderPathOf(QTreeWidgetItem *item) const
{
    if (!item)
        return QString();
    if (kindOf(item) == FolderKind)
        return item->data(0, kPayloadRole).toString();
    // A connection item: new siblings belong in the same folder as it.
    QTreeWidgetItem *parent = item->parent();
    return parent ? parent->data(0, kPayloadRole).toString() : QString();
}

QTreeWidgetItem *ConnectionTreeDock::ensureFolderItem(const QString &path, QHash<QString, QTreeWidgetItem *> &folderItems)
{
    if (path.isEmpty())
        return nullptr; // tree root

    if (auto it = folderItems.constFind(path); it != folderItems.constEnd())
        return it.value();

    const int lastSlash = path.lastIndexOf(QLatin1Char('/'));
    const QString parentPath = lastSlash >= 0 ? path.left(lastSlash) : QString();
    const QString name = lastSlash >= 0 ? path.mid(lastSlash + 1) : path;

    QTreeWidgetItem *parentItem = ensureFolderItem(parentPath, folderItems);
    auto *item = new QTreeWidgetItem();
    item->setText(0, name);
    item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
    item->setData(0, kKindRole, FolderKind);
    item->setData(0, kPayloadRole, path);

    if (parentItem)
        parentItem->addChild(item);
    else
        m_tree->addTopLevelItem(item);

    folderItems.insert(path, item);
    return item;
}

void ConnectionTreeDock::rebuildTree()
{
    m_tree->clear();
    QHash<QString, QTreeWidgetItem *> folderItems;

    // Explicitly-created empty folders first (see ProfileStore::
    // emptyFolders()) - ensureFolderItem() dedups by path, so a folder
    // that's both explicitly created *and* has real connections in it
    // still only gets one tree node, whichever loop reaches it first.
    for (const QString &folder : m_store->emptyFolders())
        ensureFolderItem(folder, folderItems);

    for (const ConnectionProfile &p : m_store->profiles()) {
        QTreeWidgetItem *parentItem = ensureFolderItem(p.folder, folderItems);

        auto *item = new QTreeWidgetItem();
        item->setText(0, p.name.isEmpty() ? QStringLiteral("(unnamed)") : p.name);
        item->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
        item->setData(0, kKindRole, ConnectionKind);
        item->setData(0, kPayloadRole, p.id.toString());

        if (parentItem)
            parentItem->addChild(item);
        else
            m_tree->addTopLevelItem(item);
    }

    m_tree->expandAll();
    m_tree->sortItems(0, Qt::AscendingOrder);
}

void ConnectionTreeDock::onItemActivated(QTreeWidgetItem *item)
{
    if (kindOf(item) != ConnectionKind)
        return;

    for (const ConnectionProfile &p : m_store->profiles()) {
        if (p.id == connectionIdOf(item)) {
            emit connectionActivated(p);
            return;
        }
    }
}

void ConnectionTreeDock::onContextMenuRequested(const QPoint &pos)
{
    QTreeWidgetItem *item = m_tree->itemAt(pos);

    QMenu menu(this);
    QAction *newAction = menu.addAction(QStringLiteral("New Connection..."));
    QAction *newFolderAction = menu.addAction(QStringLiteral("New Folder..."));
    QAction *renameAction = nullptr;
    QAction *deleteFolderAction = nullptr;
    QAction *editAction = nullptr;
    QAction *duplicateAction = nullptr;
    QAction *deleteAction = nullptr;

    if (item && kindOf(item) == FolderKind) {
        renameAction = menu.addAction(QStringLiteral("Rename Folder..."));
        menu.addSeparator();
        deleteFolderAction = menu.addAction(QStringLiteral("Delete Folder..."));
    } else if (item && kindOf(item) == ConnectionKind) {
        editAction = menu.addAction(QStringLiteral("Edit..."));
        duplicateAction = menu.addAction(QStringLiteral("Duplicate"));
        menu.addSeparator();
        deleteAction = menu.addAction(QStringLiteral("Delete"));
    }

    QAction *chosen = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;

    if (chosen == newAction) {
        ConnectionEditDialog dialog(this, folderPathOf(item));
        if (dialog.exec() == QDialog::Accepted)
            m_store->addProfile(dialog.profile());
    } else if (chosen == newFolderAction) {
        newFolder(item);
    } else if (chosen == renameAction) {
        renameFolderItem(item);
    } else if (chosen == deleteFolderAction) {
        deleteFolder(item);
    } else if (chosen == editAction) {
        editItem(item);
    } else if (chosen == duplicateAction) {
        duplicateItem(item);
    } else if (chosen == deleteAction) {
        deleteItem(item);
    }
}

void ConnectionTreeDock::newConnection()
{
    ConnectionEditDialog dialog(this, folderPathOf(m_tree->currentItem()));
    if (dialog.exec() == QDialog::Accepted)
        m_store->addProfile(dialog.profile());
}

void ConnectionTreeDock::editItem(QTreeWidgetItem *item)
{
    if (!item || kindOf(item) != ConnectionKind)
        return;

    for (const ConnectionProfile &p : m_store->profiles()) {
        if (p.id == connectionIdOf(item)) {
            ConnectionEditDialog dialog(this);
            dialog.setProfile(p);
            if (dialog.exec() == QDialog::Accepted)
                m_store->updateProfile(dialog.profile());
            return;
        }
    }
}

void ConnectionTreeDock::duplicateItem(QTreeWidgetItem *item)
{
    if (!item || kindOf(item) != ConnectionKind)
        return;

    for (const ConnectionProfile &p : m_store->profiles()) {
        if (p.id == connectionIdOf(item)) {
            ConnectionProfile copy = p;
            copy.id = QUuid::createUuid();
            copy.name = QStringLiteral("%1 (copy)").arg(p.name);
            m_store->addProfile(copy);
            return;
        }
    }
}

void ConnectionTreeDock::deleteItem(QTreeWidgetItem *item)
{
    if (!item || kindOf(item) != ConnectionKind)
        return;

    const QUuid id = connectionIdOf(item);
    const auto reply = QMessageBox::question(this, QStringLiteral("Delete Connection"),
                                              QStringLiteral("Delete \"%1\"?").arg(item->text(0)));
    if (reply == QMessageBox::Yes)
        m_store->removeProfile(id);
}

void ConnectionTreeDock::renameFolderItem(QTreeWidgetItem *item)
{
    if (!item || kindOf(item) != FolderKind)
        return;

    const QString oldPath = item->data(0, kPayloadRole).toString();
    bool ok = false;
    const QString newName = QInputDialog::getText(this, QStringLiteral("Rename Folder"), QStringLiteral("Folder name:"),
                                                    QLineEdit::Normal, item->text(0), &ok);
    if (!ok || newName.isEmpty() || newName == item->text(0))
        return;

    const int lastSlash = oldPath.lastIndexOf(QLatin1Char('/'));
    const QString parentPath = lastSlash >= 0 ? oldPath.left(lastSlash) : QString();
    const QString newPath = parentPath.isEmpty() ? newName : parentPath + QLatin1Char('/') + newName;

    m_store->renameFolder(oldPath, newPath);
}

void ConnectionTreeDock::newFolder(QTreeWidgetItem *item)
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, QStringLiteral("New Folder"), QStringLiteral("Folder name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.isEmpty())
        return;

    // Nests under `item` when it's a folder (or a folder's own current
    // folder when it's a connection) - same "where does a new thing
    // under this selection belong" logic folderPathOf() already provides
    // for New Connection.
    const QString parentPath = folderPathOf(item);
    const QString path = parentPath.isEmpty() ? name : parentPath + QLatin1Char('/') + name;
    m_store->addEmptyFolder(path);
}

int ConnectionTreeDock::countConnectionsUnder(const QString &folderPath) const
{
    int count = 0;
    for (const ConnectionProfile &p : m_store->profiles()) {
        if (p.folder == folderPath || p.folder.startsWith(folderPath + QLatin1Char('/')))
            ++count;
    }
    return count;
}

void ConnectionTreeDock::deleteFolder(QTreeWidgetItem *item)
{
    if (!item || kindOf(item) != FolderKind)
        return;

    const QString path = item->data(0, kPayloadRole).toString();
    const int count = countConnectionsUnder(path);
    const QString message = count > 0
        ? QStringLiteral("Delete folder \"%1\" and all %2 connection(s) inside it (including subfolders)? This cannot be undone.")
              .arg(item->text(0))
              .arg(count)
        : QStringLiteral("Delete empty folder \"%1\"?").arg(item->text(0));

    const auto reply = QMessageBox::question(this, QStringLiteral("Delete Folder"), message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply == QMessageBox::Yes)
        m_store->removeFolder(path);
}
