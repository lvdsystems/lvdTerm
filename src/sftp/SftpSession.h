#pragma once

#include <QThread>

#include "SftpClient.h"
#include "transports/ssh/SshConnectionSettings.h"

// GUI-thread-facing proxy in front of SftpClient, which runs on its own
// QThread - same shape as SshTransport/SshWorker (see those for why: a
// libssh2 session cannot be driven from the GUI thread). Every call
// below is marshalled into SftpClient via a queued connection; its
// signals are forwarded back out unchanged.
class SftpSession : public QObject
{
    Q_OBJECT

public:
    explicit SftpSession(SshConnectionSettings settings, QObject *parent = nullptr);
    ~SftpSession() override;

    void connectToHost();
    void disconnectFromHost();
    void listDirectory(const QString &path);
    void downloadFile(const QString &remotePath, const QString &localPath);
    void uploadFile(const QString &localPath, const QString &remotePath);
    void downloadDirectory(const QString &remotePath, const QString &localPath);
    void uploadDirectory(const QString &localPath, const QString &remotePath);
    void makeDirectory(const QString &path);
    void removeFile(const QString &path);
    void removeDirectory(const QString &path);
    void renameEntry(const QString &oldPath, const QString &newPath);

    // Called by SftpClient (on its worker thread) via a blocking queued
    // invocation - see SshConnectHelper::handshakeAndVerifyHostKey().
    Q_INVOKABLE bool confirmHostKey(const QString &message);

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
    QThread m_thread;
    SftpClient *m_client = nullptr;
};
