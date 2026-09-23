#pragma once

#include <QAbstractItemModel>
#include <QHash>

#include "SftpClient.h"

// A lazily-populated tree of the remote filesystem. Directories are
// assumed to have children (an expand arrow is always shown) until
// actually listed - there's no synchronous way to know otherwise, since
// listing happens over the network via SftpSession. The dock is
// responsible for calling requestChildren()/applyDirectoryListing()/
// markLoadFailed() at the right times (see SftpDock::onExpanded()); this
// class only owns the tree data and Qt's model/view plumbing.
class SftpModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    // Ordered to match Windows Explorer's own Details view (Name / Date
    // modified / Type / Size) so the dock reads the same way.
    enum Column
    {
        NameColumn = 0,
        ModifiedColumn = 1,
        TypeColumn = 2,
        SizeColumn = 3,
        ColumnCount,
    };

    explicit SftpModel(QObject *parent = nullptr);
    ~SftpModel() override;

    void setRoot(const QString &path);
    void applyDirectoryListing(const QString &path, const QVector<SftpEntry> &entries);
    void markLoading(const QString &path);
    void markLoadFailed(const QString &path);

    // Removes a single already-loaded child (e.g. after a successful
    // delete/rename) without a full re-list of its parent.
    void removeEntry(const QString &path);
    void addPlaceholderDirectory(const QString &parentPath, const QString &name); // optimistic UI after mkdir

    QString pathForIndex(const QModelIndex &index) const;
    bool isDirectory(const QModelIndex &index) const;
    qint64 sizeOf(const QModelIndex &index) const;
    QString nameOf(const QModelIndex &index) const;
    bool isLoaded(const QModelIndex &index) const;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;

private:
    struct Node
    {
        QString name;
        QString path;
        bool isDirectory = false;
        qint64 size = 0;
        qint64 modifiedTime = 0; // seconds since epoch, 0 = unknown (see SftpEntry::modifiedTime)
        Node *parent = nullptr;
        QVector<Node *> children;
        bool childrenLoaded = false;
        bool loading = false;

        ~Node() { qDeleteAll(children); }
    };

    Node *nodeForIndex(const QModelIndex &index) const;
    Node *findNodeByPath(const QString &path) const; // O(1) via m_byPath, see its comment
    QModelIndex indexForNode(Node *node) const;
    void insertIntoCache(Node *node);
    void removeFromCache(Node *node); // also removes node's whole subtree

    Node *m_root = nullptr;

    // Every live Node also lives here, keyed by its current path, so
    // markLoading()/applyDirectoryListing()/removeEntry()/etc. (all
    // called once per network round-trip, keyed by path rather than by
    // QModelIndex) don't need an O(tree size) walk apiece. Kept in sync
    // by insertIntoCache()/removeFromCache() at every point a node is
    // created or torn down - a node's path never changes in place once
    // inserted (a rename removes and the next listing re-adds).
    QHash<QString, Node *> m_byPath;
};
