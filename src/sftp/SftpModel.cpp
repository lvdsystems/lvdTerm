#include "SftpModel.h"

#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QStyle>

namespace
{
QString joinPath(const QString &dir, const QString &name)
{
    return dir.endsWith(QLatin1Char('/')) ? dir + name : dir + QLatin1Char('/') + name;
}

// Windows Explorer's Type column derives its friendly names from the
// registry's per-extension file associations, which makes no sense to
// replicate here for a *remote* (often Linux) filesystem - this gives the
// same general shape ("XYZ File") without pretending to know what any
// particular extension means locally.
QString typeLabel(bool isDirectory, const QString &name)
{
    if (isDirectory)
        return QStringLiteral("File folder");

    const QString suffix = QFileInfo(name).suffix();
    if (suffix.isEmpty())
        return QStringLiteral("File");
    return QStringLiteral("%1 File").arg(suffix.toUpper());
}
} // namespace

SftpModel::SftpModel(QObject *parent) : QAbstractItemModel(parent)
{
    // Never leave m_root null: the tree view queries hasChildren()/rowCount()
    // on the invalid root index as soon as it's shown, which happens at
    // app startup - well before setRoot() is called for a real connection.
    m_root = new Node;
    m_root->isDirectory = true;
    m_root->childrenLoaded = true; // empty until a real connection calls setRoot()
    insertIntoCache(m_root);
}

SftpModel::~SftpModel()
{
    delete m_root;
}

void SftpModel::setRoot(const QString &path)
{
    beginResetModel();
    delete m_root;
    m_byPath.clear();
    m_root = new Node;
    m_root->name = path;
    m_root->path = path;
    m_root->isDirectory = true;
    insertIntoCache(m_root);
    endResetModel();
}

SftpModel::Node *SftpModel::nodeForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return m_root;
    return static_cast<Node *>(index.internalPointer());
}

SftpModel::Node *SftpModel::findNodeByPath(const QString &path) const
{
    return m_byPath.value(path, nullptr);
}

void SftpModel::insertIntoCache(Node *node)
{
    m_byPath.insert(node->path, node);
}

void SftpModel::removeFromCache(Node *node)
{
    if (!node)
        return;
    m_byPath.remove(node->path);
    for (Node *child : node->children)
        removeFromCache(child);
}

QModelIndex SftpModel::indexForNode(Node *node) const
{
    if (!node || node == m_root)
        return QModelIndex();

    Node *parent = node->parent;
    const int row = parent->children.indexOf(node);
    if (row < 0)
        return QModelIndex();
    return createIndex(row, 0, node);
}

void SftpModel::markLoading(const QString &path)
{
    if (Node *node = findNodeByPath(path))
        node->loading = true;
}

void SftpModel::markLoadFailed(const QString &path)
{
    if (Node *node = findNodeByPath(path))
        node->loading = false;
}

void SftpModel::applyDirectoryListing(const QString &path, const QVector<SftpEntry> &entries)
{
    Node *node = findNodeByPath(path);
    if (!node)
        return;

    const QModelIndex parentIndex = indexForNode(node);

    if (!node->children.isEmpty()) {
        beginRemoveRows(parentIndex, 0, node->children.size() - 1);
        for (Node *child : node->children)
            removeFromCache(child);
        qDeleteAll(node->children);
        node->children.clear();
        endRemoveRows();
    }

    if (!entries.isEmpty()) {
        beginInsertRows(parentIndex, 0, entries.size() - 1);
        for (const SftpEntry &entry : entries) {
            auto *child = new Node;
            child->name = entry.name;
            child->path = joinPath(path, entry.name);
            child->isDirectory = entry.isDirectory;
            child->size = entry.size;
            child->modifiedTime = entry.modifiedTime;
            child->parent = node;
            node->children.append(child);
            insertIntoCache(child);
        }
        endInsertRows();
    }

    node->childrenLoaded = true;
    node->loading = false;
}

void SftpModel::removeEntry(const QString &path)
{
    Node *node = findNodeByPath(path);
    if (!node || !node->parent)
        return;

    Node *parent = node->parent;
    const int row = parent->children.indexOf(node);
    if (row < 0)
        return;

    const QModelIndex parentIndex = indexForNode(parent);
    beginRemoveRows(parentIndex, row, row);
    parent->children.removeAt(row);
    removeFromCache(node);
    delete node;
    endRemoveRows();
}

void SftpModel::addPlaceholderDirectory(const QString &parentPath, const QString &name)
{
    Node *parent = findNodeByPath(parentPath);
    if (!parent || !parent->childrenLoaded)
        return; // not expanded/loaded yet - the next real listing will pick it up

    const QModelIndex parentIndex = indexForNode(parent);
    const int row = parent->children.size();

    beginInsertRows(parentIndex, row, row);
    auto *child = new Node;
    child->name = name;
    child->path = joinPath(parentPath, name);
    child->isDirectory = true;
    child->parent = parent;
    parent->children.append(child);
    insertIntoCache(child);
    endInsertRows();
}

QModelIndex SftpModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return QModelIndex();

    Node *parentNode = nodeForIndex(parent);
    if (row < 0 || row >= parentNode->children.size())
        return QModelIndex();

    return createIndex(row, column, parentNode->children.at(row));
}

QModelIndex SftpModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return QModelIndex();

    Node *node = nodeForIndex(child);
    return indexForNode(node->parent);
}

int SftpModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;
    return nodeForIndex(parent)->children.size();
}

int SftpModel::columnCount(const QModelIndex & /*parent*/) const
{
    return ColumnCount;
}

bool SftpModel::hasChildren(const QModelIndex &parent) const
{
    Node *node = nodeForIndex(parent);
    if (!node->isDirectory)
        return false;
    // Optimistic until actually listed - there is no synchronous way to
    // know whether a remote directory is empty.
    return !node->childrenLoaded || !node->children.isEmpty();
}

QVariant SftpModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    Node *node = nodeForIndex(index);

    if (role == Qt::DisplayRole) {
        if (index.column() == NameColumn)
            return node->loading ? QStringLiteral("%1 (loading…)").arg(node->name) : node->name;
        if (index.column() == SizeColumn)
            return node->isDirectory ? QString() : QLocale().formattedDataSize(node->size);
        if (index.column() == TypeColumn)
            return typeLabel(node->isDirectory, node->name);
        if (index.column() == ModifiedColumn) {
            return node->modifiedTime > 0
                ? QLocale().toString(QDateTime::fromSecsSinceEpoch(node->modifiedTime), QLocale::ShortFormat)
                : QString();
        }
    }

    if (role == Qt::DecorationRole && index.column() == NameColumn) {
        return QApplication::style()->standardIcon(node->isDirectory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon);
    }

    return {};
}

QVariant SftpModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case NameColumn:
        return QStringLiteral("Name");
    case ModifiedColumn:
        return QStringLiteral("Date modified");
    case TypeColumn:
        return QStringLiteral("Type");
    case SizeColumn:
        return QStringLiteral("Size");
    default:
        return {};
    }
}

Qt::ItemFlags SftpModel::flags(const QModelIndex &index) const
{
    // QAbstractItemModel::flags()'s own default omits ItemIsDragEnabled/
    // ItemIsDropEnabled (unlike e.g. QStandardItemModel, whose items grant
    // both by default) - without this override, SftpTreeView's
    // setDragEnabled(true) alone was never enough: QAbstractItemView also
    // requires the *model*, per item, to grant ItemIsDragEnabled before it
    // will ever start an item drag, so every click-and-drag fell through
    // to ExtendedSelection's other built-in gesture (extending the
    // selection across whatever rows the mouse passed over) instead.
    if (!index.isValid())
        return Qt::NoItemFlags;

    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    if (isDirectory(index))
        result |= Qt::ItemIsDropEnabled; // upload-by-drop onto a directory row, see SftpTreeView::dropEvent()
    return result;
}

QString SftpModel::pathForIndex(const QModelIndex &index) const
{
    return nodeForIndex(index)->path;
}

bool SftpModel::isDirectory(const QModelIndex &index) const
{
    return nodeForIndex(index)->isDirectory;
}

qint64 SftpModel::sizeOf(const QModelIndex &index) const
{
    return nodeForIndex(index)->size;
}

QString SftpModel::nameOf(const QModelIndex &index) const
{
    return nodeForIndex(index)->name;
}

bool SftpModel::isLoaded(const QModelIndex &index) const
{
    return nodeForIndex(index)->childrenLoaded;
}
