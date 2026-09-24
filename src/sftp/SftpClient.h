#pragma once

#include <libssh2.h>
#include <libssh2_sftp.h>

#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QVector>

#include "transports/ssh/SshConnectionSettings.h"

struct SftpEntry
{
    QString name;
    bool isDirectory = false;
    qint64 size = 0;
    unsigned long permissions = 0;
    qint64 modifiedTime = 0; // seconds since epoch, 0 if the server didn't report it
};
Q_DECLARE_METATYPE(SftpEntry)

// A second, independent SSH+SFTP session to the same host as an
// interactive SSH pane ("SFTP dock uses a second SSH session", since a
// libssh2 session/channel can't safely be shared between an interactive
// pty shell and file transfers). Runs entirely
// blocking on its own QThread: unlike SshWorker's terminal session,
// which must interleave reads and writes as they happen, every
// operation here is a self-contained request/response round-trip, so a
// worker thread that just blocks until each one finishes is simpler and
// just as effective - and queued slot invocations from the GUI thread
// naturally serialize into a one-at-a-time transfer queue for free
// (the thread's event loop dispatches the next queued call only once
// the current blocking slot returns).
//
// Connect/verify/auth logic is shared with SshWorker via
// SshConnectHelper - both sessions must agree on what "trusted" means.
class SftpClient : public QObject
{
    Q_OBJECT

public:
    // hostKeyPromptTarget must live on the GUI thread and have a
    // Q_INVOKABLE "bool confirmHostKey(QString)" - see
    // SshConnectHelper::handshakeAndVerifyHostKey().
    SftpClient(SshConnectionSettings settings, QObject *hostKeyPromptTarget);
    ~SftpClient() override;

public slots:
    void connectToHost();
    void disconnectFromHost();

    void listDirectory(const QString &path);
    void downloadFile(const QString &remotePath, const QString &localPath);
    void uploadFile(const QString &localPath, const QString &remotePath);
    // Recursive: mirrors the whole subtree, creating directories as
    // needed on the destination side. Emits transferStarted/Progress/
    // Finished per file as it goes, then one operationSucceeded/Failed
    // for the directory as a whole once every file is done.
    void downloadDirectory(const QString &remotePath, const QString &localPath);
    void uploadDirectory(const QString &localPath, const QString &remotePath);
    void makeDirectory(const QString &path);
    void removeFile(const QString &path);
    void removeDirectory(const QString &path); // recursive - see .cpp
    void renameEntry(const QString &oldPath, const QString &newPath);

signals:
    void connected(const QString &homePath);
    void connectFailed(const QString &message);
    void disconnected();

    void directoryListed(const QString &path, const QVector<SftpEntry> &entries);
    void operationFailed(const QString &operation, const QString &path, const QString &message);
    void operationSucceeded(const QString &operation, const QString &path);

    void transferStarted(const QString &path, qint64 totalBytes);
    void transferProgress(const QString &path, qint64 bytesDone, qint64 totalBytes);
    void transferFinished(const QString &path, bool ok);

private:
    void teardown();
    QString lastSftpError() const;

    // Shared by the single-file and recursive-directory entry points, so
    // a directory transfer doesn't emit a misleading operationSucceeded
    // for every file in the tree - only the caller (uploadFile/
    // downloadFile, or the *Recursive helpers below) decides when the
    // *requested* operation is done. Still emit transferStarted/Progress/
    // Finished per file either way, since that's what drives the
    // progress bar.
    bool uploadFileInternal(const QString &localPath, const QString &remotePath, QString *errorMessage);
    bool downloadFileInternal(const QString &remotePath, const QString &localPath, QString *errorMessage);
    bool uploadDirectoryRecursive(const QString &localPath, const QString &remotePath, QString *errorMessage);
    bool downloadDirectoryRecursive(const QString &remotePath, const QString &localPath, QString *errorMessage);
    bool removeRecursively(const QString &path, QString *errorMessage);

    SshConnectionSettings m_settings;
    // QPointer, not a raw QObject*: SftpSession can now be destroyed
    // (see ~SftpSession()) while this client is still detached and
    // running on its own thread, mid-connect - this must safely go null
    // rather than dangle if that happens before the host-key prompt.
    QPointer<QObject> m_hostKeyPromptTarget;

    libssh2_socket_t m_socket = LIBSSH2_INVALID_SOCKET;
    LIBSSH2_SESSION *m_session = nullptr;
    LIBSSH2_SFTP *m_sftp = nullptr;
};
