#include "SessionStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace
{
QString storagePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/session.json");
}

QJsonObject nodeToJson(const SessionNode &node)
{
    QJsonObject o;
    if (node.kind == SessionNode::Kind::Leaf) {
        o[QStringLiteral("kind")] = QStringLiteral("leaf");
        if (node.connection)
            o[QStringLiteral("connection")] = node.connection->toJson();
    } else {
        o[QStringLiteral("kind")] = QStringLiteral("splitter");
        o[QStringLiteral("orientation")] = node.orientation == Qt::Horizontal ? QStringLiteral("horizontal") : QStringLiteral("vertical");

        QJsonArray sizes;
        for (int size : node.sizes)
            sizes.append(size);
        o[QStringLiteral("sizes")] = sizes;

        QJsonArray children;
        for (const SessionNode &child : node.children)
            children.append(nodeToJson(child));
        o[QStringLiteral("children")] = children;
    }
    return o;
}

std::optional<SessionNode> nodeFromJson(const QJsonObject &o)
{
    SessionNode node;
    if (o[QStringLiteral("kind")].toString() == QStringLiteral("splitter")) {
        node.kind = SessionNode::Kind::Splitter;
        node.orientation = o[QStringLiteral("orientation")].toString() == QStringLiteral("vertical") ? Qt::Vertical : Qt::Horizontal;

        for (const QJsonValue &v : o[QStringLiteral("sizes")].toArray())
            node.sizes.append(v.toInt());

        for (const QJsonValue &v : o[QStringLiteral("children")].toArray()) {
            if (!v.isObject())
                continue;
            if (std::optional<SessionNode> child = nodeFromJson(v.toObject()))
                node.children.append(*child);
        }
        if (node.children.isEmpty())
            return std::nullopt; // an all-unpersistable (or empty) splitter is pointless to restore
        return node;
    }

    node.kind = SessionNode::Kind::Leaf;
    if (!o.contains(QStringLiteral("connection")))
        return std::nullopt; // shouldn't happen (see nodeToJson) - a leaf is only ever written when it has a connection
    node.connection = ConnectionProfile::fromJson(o[QStringLiteral("connection")].toObject());
    return node;
}
} // namespace

namespace SessionStore
{

SessionState load()
{
    SessionState state;

    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly))
        return state;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return state;

    const QJsonObject root = doc.object();
    state.windowGeometry = QByteArray::fromBase64(root[QStringLiteral("windowGeometry")].toString().toLatin1());
    state.windowState = QByteArray::fromBase64(root[QStringLiteral("windowState")].toString().toLatin1());
    state.currentTabIndex = root[QStringLiteral("currentTabIndex")].toInt();

    for (const QJsonValue &v : root[QStringLiteral("tabs")].toArray()) {
        if (!v.isObject())
            continue;
        const QJsonObject tabObject = v.toObject();
        if (!tabObject.contains(QStringLiteral("root")))
            continue;
        if (std::optional<SessionNode> root_ = nodeFromJson(tabObject[QStringLiteral("root")].toObject()))
            state.tabs.append(SessionTab{tabObject[QStringLiteral("title")].toString(), *root_});
    }

    return state;
}

void save(const SessionState &state)
{
    QJsonObject root;
    root[QStringLiteral("windowGeometry")] = QString::fromLatin1(state.windowGeometry.toBase64());
    root[QStringLiteral("windowState")] = QString::fromLatin1(state.windowState.toBase64());
    root[QStringLiteral("currentTabIndex")] = state.currentTabIndex;

    QJsonArray tabs;
    for (const SessionTab &tab : state.tabs) {
        QJsonObject tabObject;
        tabObject[QStringLiteral("title")] = tab.title;
        tabObject[QStringLiteral("root")] = nodeToJson(tab.root);
        tabs.append(tabObject);
    }
    root[QStringLiteral("tabs")] = tabs;

    QFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

} // namespace SessionStore
