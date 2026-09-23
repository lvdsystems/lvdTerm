#include "SftpClient.h"

#include <algorithm>

#include <winsock2.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "transports/ssh/SshConnectHelper.h"

namespace
{
QString joinPath(const QString &dir, const QString &name)
{
    return dir.endsWith(QLatin1Char('/')) ? dir + name : dir + QLatin1Char('/') + name;
}

QString sftpErrorString(unsigned long code)
{
    switch (code) {
    case LIBSSH2_FX_OK:
        return QStringLiteral("OK");
    case LIBSSH2_FX_EOF:
        return QStringLiteral("end of file");
    case LIBSSH2_FX_NO_SUCH_FILE:
        return QStringLiteral("no such file");
    case LIBSSH2_FX_PERMISSION_DENIED:
        return QStringLiteral("permission denied");
    case LIBSSH2_FX_NO_SUCH_PATH:
        return QStringLiteral("no such path");
    case LIBSSH2_FX_FILE_ALREADY_EXISTS:
        return QStringLiteral("file already exists");
    case LIBSSH2_FX_DIR_NOT_EMPTY:
        return QStringLiteral("directory not empty");
    case LIBSSH2_FX_NOT_A_DIRECTORY:
        return QStringLiteral("not a directory");
    case LIBSSH2_FX_NO_SPACE_ON_FILESYSTEM:
        return QStringLiteral("no space left on device");
    default:
        return QStringLiteral("SFTP error %1").arg(code);
    }
}
} // namespace

SftpClient::SftpClient(SshConnectionSettings settings, QObject *hostKeyPromptTarget)
    : m_settings(std::move(settings))
    , m_hostKeyPromptTarget(hostKeyPromptTarget)
{
    // Needed for directoryListed(QString, QVector<SftpEntry>) to cross
    // the GUI/worker thread boundary via a queued connection.
    qRegisterMetaType<QVector<SftpEntry>>("QVector<SftpEntry>");
}

SftpClient::~SftpClient()
{
    teardown();
}

void SftpClient::connectToHost()
{
    QString error;

    m_socket = SshConnectHelper::connectSocket(m_settings.host, m_settings.port, &error);
    if (m_socket == LIBSSH2_INVALID_SOCKET) {
        emit connectFailed(error);
        return;
    }

    m_session = libssh2_session_init();
    if (!m_session) {
        emit connectFailed(QStringLiteral("Could not allocate SSH session"));
        teardown();
        return;
    }

    if (!SshConnectHelper::handshakeAndVerifyHostKey(m_session, m_socket, m_settings.host, m_settings.port, m_hostKeyPromptTarget, &error)) {
        emit connectFailed(error);
        teardown();
        return;
    }

    if (!SshConnectHelper::authenticate(m_session, m_settings, &error)) {
        emit connectFailed(error);
        teardown();
        return;
    }

    m_sftp = libssh2_sftp_init(m_session);
    if (!m_sftp) {
        emit connectFailed(QStringLiteral("Could not start SFTP subsystem"));
        teardown();
        return;
    }

    // "." resolves to the connecting user's default/home directory on
    // essentially every real SFTP server; realpath() turns that into an
    // absolute path we can build child paths on top of.
    char realPathBuf[1024];
    const int len = libssh2_sftp_realpath(m_sftp, ".", realPathBuf, sizeof(realPathBuf));
    const QString homePath = len > 0 ? QString::fromUtf8(realPathBuf, len) : QStringLiteral("/");

    emit connected(homePath);
}

void SftpClient::disconnectFromHost()
{
    teardown();
    emit disconnected();
}

QString SftpClient::lastSftpError() const
{
    return m_sftp ? sftpErrorString(libssh2_sftp_last_error(m_sftp)) : QStringLiteral("not connected");
}

void SftpClient::listDirectory(const QString &path)
{
    if (!m_sftp) {
        emit operationFailed(QStringLiteral("list"), path, QStringLiteral("Not connected"));
        return;
    }

    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_opendir(m_sftp, path.toUtf8().constData());
    if (!handle) {
        emit operationFailed(QStringLiteral("list"), path, lastSftpError());
        return;
    }

    QVector<SftpEntry> entries;
    char nameBuf[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;

    int rc;
    while ((rc = libssh2_sftp_readdir(handle, nameBuf, sizeof(nameBuf), &attrs)) > 0) {
        const QString name = QString::fromUtf8(nameBuf, rc);
        if (name == QStringLiteral(".") || name == QStringLiteral(".."))
            continue;

        SftpEntry entry;
        entry.name = name;
        entry.isDirectory = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
        entry.size = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? static_cast<qint64>(attrs.filesize) : 0;
        entry.permissions = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ? attrs.permissions : 0;
        entry.modifiedTime = (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? static_cast<qint64>(attrs.mtime) : 0;
        entries.append(entry);
    }
    libssh2_sftp_closedir(handle);

    std::sort(entries.begin(), entries.end(), [](const SftpEntry &a, const SftpEntry &b) {
        if (a.isDirectory != b.isDirectory)
            return a.isDirectory;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    emit directoryListed(path, entries);
}

bool SftpClient::downloadFileInternal(const QString &remotePath, const QString &localPath, QString *errorMessage)
{
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open(m_sftp, remotePath.toUtf8().constData(), LIBSSH2_FXF_READ, 0);
    if (!handle) {
        *errorMessage = lastSftpError();
        emit transferFinished(remotePath, false);
        return false;
    }

    LIBSSH2_SFTP_ATTRIBUTES attrs;
    libssh2_sftp_fstat(handle, &attrs);
    const qint64 totalSize = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? static_cast<qint64>(attrs.filesize) : -1;

    QFile localFile(localPath);
    if (!localFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        libssh2_sftp_close(handle);
        *errorMessage = QStringLiteral("Could not create local file: %1").arg(localPath);
        emit transferFinished(remotePath, false);
        return false;
    }

    emit transferStarted(remotePath, totalSize);

    char buffer[65536];
    qint64 done = 0;
    bool ok = true;
    ssize_t n;
    while ((n = libssh2_sftp_read(handle, buffer, sizeof(buffer))) > 0) {
        if (localFile.write(buffer, n) != n) {
            ok = false;
            break;
        }
        done += n;
        emit transferProgress(remotePath, done, totalSize);
    }
    if (n < 0) {
        *errorMessage = lastSftpError();
        ok = false;
    }

    localFile.close();
    libssh2_sftp_close(handle);

    if (!ok)
        QFile::remove(localPath); // don't leave a truncated/partial file behind

    emit transferFinished(remotePath, ok);
    return ok;
}

void SftpClient::downloadFile(const QString &remotePath, const QString &localPath)
{
    if (!m_sftp) {
        emit transferFinished(remotePath, false);
        emit operationFailed(QStringLiteral("download"), remotePath, QStringLiteral("Not connected"));
        return;
    }

    QString error;
    if (!downloadFileInternal(remotePath, localPath, &error)) {
        emit operationFailed(QStringLiteral("download"), remotePath, error);
        return;
    }

    emit operationSucceeded(QStringLiteral("download"), remotePath);
}

bool SftpClient::uploadFileInternal(const QString &localPath, const QString &remotePath, QString *errorMessage)
{
    QFile localFile(localPath);
    if (!localFile.open(QIODevice::ReadOnly)) {
        *errorMessage = QStringLiteral("Could not open local file: %1").arg(localPath);
        emit transferFinished(remotePath, false);
        return false;
    }

    const qint64 totalSize = localFile.size();

    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_open(m_sftp, remotePath.toUtf8().constData(),
                                                     LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, 0644);
    if (!handle) {
        *errorMessage = lastSftpError();
        emit transferFinished(remotePath, false);
        return false;
    }

    emit transferStarted(remotePath, totalSize);

    char buffer[65536];
    qint64 done = 0;
    bool ok = true;
    while (!localFile.atEnd()) {
        const qint64 n = localFile.read(buffer, sizeof(buffer));
        if (n < 0) {
            ok = false;
            break;
        }

        qint64 written = 0;
        while (written < n) {
            const ssize_t w = libssh2_sftp_write(handle, buffer + written, static_cast<size_t>(n - written));
            if (w < 0) {
                ok = false;
                break;
            }
            written += w;
        }
        if (!ok)
            break;

        done += n;
        emit transferProgress(remotePath, done, totalSize);
    }

    if (!ok)
        *errorMessage = lastSftpError();

    libssh2_sftp_close(handle);

    if (!ok)
        libssh2_sftp_unlink(m_sftp, remotePath.toUtf8().constData()); // don't leave a partial file behind

    emit transferFinished(remotePath, ok);
    return ok;
}

void SftpClient::uploadFile(const QString &localPath, const QString &remotePath)
{
    if (!m_sftp) {
        emit transferFinished(remotePath, false);
        emit operationFailed(QStringLiteral("upload"), remotePath, QStringLiteral("Not connected"));
        return;
    }

    QString error;
    if (!uploadFileInternal(localPath, remotePath, &error)) {
        emit operationFailed(QStringLiteral("upload"), remotePath, error);
        return;
    }

    emit operationSucceeded(QStringLiteral("upload"), remotePath);
}

bool SftpClient::downloadDirectoryRecursive(const QString &remotePath, const QString &localPath, QString *errorMessage)
{
    if (!QDir().mkpath(localPath)) {
        *errorMessage = QStringLiteral("Could not create local directory: %1").arg(localPath);
        return false;
    }

    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_opendir(m_sftp, remotePath.toUtf8().constData());
    if (!handle) {
        *errorMessage = lastSftpError();
        return false;
    }

    struct Child
    {
        QString name;
        bool isDirectory;
    };
    QVector<Child> children;
    char nameBuf[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc;
    while ((rc = libssh2_sftp_readdir(handle, nameBuf, sizeof(nameBuf), &attrs)) > 0) {
        const QString name = QString::fromUtf8(nameBuf, rc);
        if (name == QStringLiteral(".") || name == QStringLiteral(".."))
            continue;
        const bool isDir = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
        children.append({name, isDir});
    }
    libssh2_sftp_closedir(handle);

    for (const Child &child : children) {
        const QString childRemote = joinPath(remotePath, child.name);
        const QString childLocal = localPath + QLatin1Char('/') + child.name;
        const bool ok = child.isDirectory ? downloadDirectoryRecursive(childRemote, childLocal, errorMessage)
                                           : downloadFileInternal(childRemote, childLocal, errorMessage);
        if (!ok)
            return false;
    }
    return true;
}

void SftpClient::downloadDirectory(const QString &remotePath, const QString &localPath)
{
    if (!m_sftp) {
        emit operationFailed(QStringLiteral("download"), remotePath, QStringLiteral("Not connected"));
        return;
    }

    QString error;
    if (!downloadDirectoryRecursive(remotePath, localPath, &error)) {
        emit operationFailed(QStringLiteral("download"), remotePath, error);
        return;
    }

    emit operationSucceeded(QStringLiteral("download"), remotePath);
}

bool SftpClient::uploadDirectoryRecursive(const QString &localPath, const QString &remotePath, QString *errorMessage)
{
    if (libssh2_sftp_mkdir(m_sftp, remotePath.toUtf8().constData(), 0755) != 0) {
        // Servers disagree on the exact error for "already exists" on
        // mkdir (SSH_FX_FAILURE vs SSH_FX_FILE_ALREADY_EXISTS) - stat it
        // instead of pattern-matching the error code, so re-uploading
        // into an existing tree works everywhere.
        LIBSSH2_SFTP_ATTRIBUTES attrs;
        const bool existsAsDir = libssh2_sftp_stat(m_sftp, remotePath.toUtf8().constData(), &attrs) == 0
            && (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
        if (!existsAsDir) {
            *errorMessage = lastSftpError();
            return false;
        }
    }

    const QFileInfoList entries = QDir(localPath).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : entries) {
        const QString childRemote = joinPath(remotePath, info.fileName());
        const bool ok = info.isDir() ? uploadDirectoryRecursive(info.absoluteFilePath(), childRemote, errorMessage)
                                      : uploadFileInternal(info.absoluteFilePath(), childRemote, errorMessage);
        if (!ok)
            return false;
    }
    return true;
}

void SftpClient::uploadDirectory(const QString &localPath, const QString &remotePath)
{
    if (!m_sftp) {
        emit operationFailed(QStringLiteral("upload"), remotePath, QStringLiteral("Not connected"));
        return;
    }

    QString error;
    if (!uploadDirectoryRecursive(localPath, remotePath, &error)) {
        emit operationFailed(QStringLiteral("upload"), remotePath, error);
        return;
    }

    emit operationSucceeded(QStringLiteral("upload"), remotePath);
}

void SftpClient::makeDirectory(const QString &path)
{
    if (!m_sftp) {
        emit operationFailed(QStringLiteral("mkdir"), path, QStringLiteral("Not connected"));
        return;
    }

    if (libssh2_sftp_mkdir(m_sftp, path.toUtf8().constData(), 0755) != 0) {
        emit operationFailed(QStringLiteral("mkdir"), path, lastSftpError());
        return;
    }

    emit operationSucceeded(QStringLiteral("mkdir"), path);
}

void SftpClient::removeFile(const QString &path)
{
    if (!m_sftp) {
        emit operationFailed(QStringLiteral("delete"), path, QStringLiteral("Not connected"));
        return;
    }

    if (libssh2_sftp_unlink(m_sftp, path.toUtf8().constData()) != 0) {
        emit operationFailed(QStringLiteral("delete"), path, lastSftpError());
        return;
    }

    emit operationSucceeded(QStringLiteral("delete"), path);
}

bool SftpClient::removeRecursively(const QString &path, QString *errorMessage)
{
    LIBSSH2_SFTP_HANDLE *handle = libssh2_sftp_opendir(m_sftp, path.toUtf8().constData());
    if (!handle) {
        *errorMessage = lastSftpError();
        return false;
    }

    struct Child
    {
        QString name;
        bool isDirectory;
    };
    QVector<Child> children;
    char nameBuf[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc;
    while ((rc = libssh2_sftp_readdir(handle, nameBuf, sizeof(nameBuf), &attrs)) > 0) {
        const QString name = QString::fromUtf8(nameBuf, rc);
        if (name == QStringLiteral(".") || name == QStringLiteral(".."))
            continue;
        const bool isDir = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
        children.append({name, isDir});
    }
    libssh2_sftp_closedir(handle);

    for (const Child &child : children) {
        const QString childPath = joinPath(path, child.name);
        const bool ok = child.isDirectory ? removeRecursively(childPath, errorMessage)
                                           : (libssh2_sftp_unlink(m_sftp, childPath.toUtf8().constData()) == 0);
        if (!ok) {
            if (errorMessage->isEmpty())
                *errorMessage = lastSftpError();
            return false;
        }
    }

    if (libssh2_sftp_rmdir(m_sftp, path.toUtf8().constData()) != 0) {
        *errorMessage = lastSftpError();
        return false;
    }
    return true;
}

void SftpClient::removeDirectory(const QString &path)
{
    if (!m_sftp) {
        emit operationFailed(QStringLiteral("delete"), path, QStringLiteral("Not connected"));
        return;
    }

    // Fast path: an empty directory needs no listing round-trip at all.
    // Only fall back to walking it (and deleting every entry found) if
    // that fails - covers both "not empty" and "doesn't exist" the same
    // way removeRecursively's own opendir error would.
    if (libssh2_sftp_rmdir(m_sftp, path.toUtf8().constData()) == 0) {
        emit operationSucceeded(QStringLiteral("delete"), path);
        return;
    }

    QString error;
    if (!removeRecursively(path, &error)) {
        emit operationFailed(QStringLiteral("delete"), path, error);
        return;
    }

    emit operationSucceeded(QStringLiteral("delete"), path);
}

void SftpClient::renameEntry(const QString &oldPath, const QString &newPath)
{
    if (!m_sftp) {
        emit operationFailed(QStringLiteral("rename"), oldPath, QStringLiteral("Not connected"));
        return;
    }

    if (libssh2_sftp_rename(m_sftp, oldPath.toUtf8().constData(), newPath.toUtf8().constData()) != 0) {
        emit operationFailed(QStringLiteral("rename"), oldPath, lastSftpError());
        return;
    }

    emit operationSucceeded(QStringLiteral("rename"), oldPath);
}

void SftpClient::teardown()
{
    if (m_sftp) {
        libssh2_sftp_shutdown(m_sftp);
        m_sftp = nullptr;
    }
    if (m_session) {
        libssh2_session_disconnect(m_session, "lvdterm SFTP session closing");
        libssh2_session_free(m_session);
        m_session = nullptr;
    }
    if (m_socket != LIBSSH2_INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = LIBSSH2_INVALID_SOCKET;
        WSACleanup();
    }
}
