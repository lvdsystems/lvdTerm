#pragma once

#include <QObject>
#include <QVector>

#include "ConnectionProfile.h"

// Owns the in-memory list of saved connections and persists it to
// %APPDATA%/lvdterm/connections.json. Loads once at construction; every
// mutation saves immediately (there are only ever a handful of profiles,
// so debouncing isn't worth the complexity).
class ProfileStore : public QObject
{
    Q_OBJECT

public:
    explicit ProfileStore(QObject *parent = nullptr);

    const QVector<ConnectionProfile> &profiles() const { return m_profiles; }

    void addProfile(const ConnectionProfile &profile);
    void updateProfile(const ConnectionProfile &profile); // matched by id
    void removeProfile(const QUuid &id);

    // Renames a folder and every profile under it (and its subfolders).
    // oldPath/newPath are "/"-separated, e.g. "Work" -> "Work Servers".
    void renameFolder(const QString &oldPath, const QString &newPath);

    // Folders are normally implied purely by ConnectionProfile::folder -
    // they exist the moment any profile uses that path and disappear
    // once none do (see ConnectionTreeDock). That's not enough for a
    // "New Folder" action, which needs to create - and persist - a
    // folder with nothing in it yet. emptyFolders() is exactly that: a
    // separate, explicitly-created list of "/"-separated paths (nested
    // the same way profile folders are), independent of whether any
    // profile currently uses them.
    const QVector<QString> &emptyFolders() const { return m_emptyFolders; }
    void addEmptyFolder(const QString &path);
    // Deletes every profile and every empty-folder entry at or under
    // path (i.e. the folder itself and all its subfolders) - the
    // confirmation prompt for this lives in ConnectionTreeDock.
    void removeFolder(const QString &path);

signals:
    void changed();

private:
    static QString storagePath();
    void load();
    void save();

    QVector<ConnectionProfile> m_profiles;
    QVector<QString> m_emptyFolders;
};
