#include "ProfileStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

ProfileStore::ProfileStore(QObject *parent) : QObject(parent)
{
    load();
}

QString ProfileStore::storagePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/connections.json");
}

void ProfileStore::load()
{
    QFile file(storagePath());
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    m_profiles.clear();
    m_emptyFolders.clear();

    if (doc.isArray()) {
        // Pre-empty-folders format: a bare array of profiles.
        for (const QJsonValue &v : doc.array()) {
            if (v.isObject())
                m_profiles.append(ConnectionProfile::fromJson(v.toObject()));
        }
    } else if (doc.isObject()) {
        const QJsonObject root = doc.object();
        for (const QJsonValue &v : root.value(QStringLiteral("profiles")).toArray()) {
            if (v.isObject())
                m_profiles.append(ConnectionProfile::fromJson(v.toObject()));
        }
        for (const QJsonValue &v : root.value(QStringLiteral("emptyFolders")).toArray()) {
            if (v.isString())
                m_emptyFolders.append(v.toString());
        }
    }
}

void ProfileStore::save()
{
    QJsonArray profilesArray;
    for (const ConnectionProfile &p : m_profiles)
        profilesArray.append(p.toJson());

    QJsonArray emptyFoldersArray;
    for (const QString &folder : m_emptyFolders)
        emptyFoldersArray.append(folder);

    QJsonObject root;
    root[QStringLiteral("profiles")] = profilesArray;
    root[QStringLiteral("emptyFolders")] = emptyFoldersArray;

    QFile file(storagePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void ProfileStore::addProfile(const ConnectionProfile &profile)
{
    m_profiles.append(profile);
    save();
    emit changed();
}

void ProfileStore::updateProfile(const ConnectionProfile &profile)
{
    for (ConnectionProfile &p : m_profiles) {
        if (p.id == profile.id) {
            p = profile;
            save();
            emit changed();
            return;
        }
    }
}

void ProfileStore::removeProfile(const QUuid &id)
{
    const auto before = m_profiles.size();
    m_profiles.removeIf([&id](const ConnectionProfile &p) { return p.id == id; });
    if (m_profiles.size() != before) {
        save();
        emit changed();
    }
}

void ProfileStore::renameFolder(const QString &oldPath, const QString &newPath)
{
    if (oldPath == newPath)
        return;

    bool anyChanged = false;
    for (ConnectionProfile &p : m_profiles) {
        if (p.folder == oldPath) {
            p.folder = newPath;
            anyChanged = true;
        } else if (p.folder.startsWith(oldPath + QLatin1Char('/'))) {
            p.folder = newPath + p.folder.mid(oldPath.size());
            anyChanged = true;
        }
    }

    if (anyChanged) {
        save();
        emit changed();
    }
}

void ProfileStore::addEmptyFolder(const QString &path)
{
    if (path.isEmpty() || m_emptyFolders.contains(path))
        return;

    m_emptyFolders.append(path);
    save();
    emit changed();
}

void ProfileStore::removeFolder(const QString &path)
{
    if (path.isEmpty())
        return;

    const auto matches = [&path](const QString &folder) { return folder == path || folder.startsWith(path + QLatin1Char('/')); };

    const auto profileCountBefore = m_profiles.size();
    m_profiles.removeIf([&](const ConnectionProfile &p) { return matches(p.folder); });

    const auto emptyCountBefore = m_emptyFolders.size();
    m_emptyFolders.removeIf(matches);

    if (m_profiles.size() != profileCountBefore || m_emptyFolders.size() != emptyCountBefore) {
        save();
        emit changed();
    }
}
