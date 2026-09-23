#include "SftpSession.h"

#include <QMessageBox>
#include <QMetaObject>

SftpSession::SftpSession(SshConnectionSettings settings, QObject *parent)
    : QObject(parent)
    , m_client(new SftpClient(std::move(settings), this))
{
    m_client->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_client, &QObject::deleteLater);

    connect(m_client, &SftpClient::connected, this, &SftpSession::connected);
    connect(m_client, &SftpClient::connectFailed, this, &SftpSession::connectFailed);
    connect(m_client, &SftpClient::disconnected, this, &SftpSession::disconnected);
    connect(m_client, &SftpClient::directoryListed, this, &SftpSession::directoryListed);
    connect(m_client, &SftpClient::operationFailed, this, &SftpSession::operationFailed);
    connect(m_client, &SftpClient::operationSucceeded, this, &SftpSession::operationSucceeded);
    connect(m_client, &SftpClient::transferStarted, this, &SftpSession::transferStarted);
    connect(m_client, &SftpClient::transferProgress, this, &SftpSession::transferProgress);
    connect(m_client, &SftpClient::transferFinished, this, &SftpSession::transferFinished);

    m_thread.start();
}

SftpSession::~SftpSession()
{
    QMetaObject::invokeMethod(m_client, "disconnectFromHost", Qt::QueuedConnection);
    m_thread.quit();
    m_thread.wait(3000);
}

void SftpSession::connectToHost()
{
    QMetaObject::invokeMethod(m_client, "connectToHost", Qt::QueuedConnection);
}

void SftpSession::disconnectFromHost()
{
    QMetaObject::invokeMethod(m_client, "disconnectFromHost", Qt::QueuedConnection);
}

void SftpSession::listDirectory(const QString &path)
{
    QMetaObject::invokeMethod(m_client, "listDirectory", Qt::QueuedConnection, Q_ARG(QString, path));
}

void SftpSession::downloadFile(const QString &remotePath, const QString &localPath)
{
    QMetaObject::invokeMethod(m_client, "downloadFile", Qt::QueuedConnection, Q_ARG(QString, remotePath), Q_ARG(QString, localPath));
}

void SftpSession::uploadFile(const QString &localPath, const QString &remotePath)
{
    QMetaObject::invokeMethod(m_client, "uploadFile", Qt::QueuedConnection, Q_ARG(QString, localPath), Q_ARG(QString, remotePath));
}

void SftpSession::downloadDirectory(const QString &remotePath, const QString &localPath)
{
    QMetaObject::invokeMethod(m_client, "downloadDirectory", Qt::QueuedConnection, Q_ARG(QString, remotePath), Q_ARG(QString, localPath));
}

void SftpSession::uploadDirectory(const QString &localPath, const QString &remotePath)
{
    QMetaObject::invokeMethod(m_client, "uploadDirectory", Qt::QueuedConnection, Q_ARG(QString, localPath), Q_ARG(QString, remotePath));
}

void SftpSession::makeDirectory(const QString &path)
{
    QMetaObject::invokeMethod(m_client, "makeDirectory", Qt::QueuedConnection, Q_ARG(QString, path));
}

void SftpSession::removeFile(const QString &path)
{
    QMetaObject::invokeMethod(m_client, "removeFile", Qt::QueuedConnection, Q_ARG(QString, path));
}

void SftpSession::removeDirectory(const QString &path)
{
    QMetaObject::invokeMethod(m_client, "removeDirectory", Qt::QueuedConnection, Q_ARG(QString, path));
}

void SftpSession::renameEntry(const QString &oldPath, const QString &newPath)
{
    QMetaObject::invokeMethod(m_client, "renameEntry", Qt::QueuedConnection, Q_ARG(QString, oldPath), Q_ARG(QString, newPath));
}

bool SftpSession::confirmHostKey(const QString &message)
{
    return QMessageBox::warning(nullptr, QStringLiteral("SSH Host Key"), message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        == QMessageBox::Yes;
}
