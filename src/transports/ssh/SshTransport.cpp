#include "SshTransport.h"

#include <QMessageBox>

#include "SshWorker.h"

SshTransport::SshTransport(SshConnectionSettings settings, QObject *parent)
    : Transport(parent)
    , m_settings(std::move(settings))
    , m_worker(new SshWorker(m_settings, this))
{
    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &SshWorker::connected, this, [this] { setState(State::Connected); });
    connect(m_worker, &SshWorker::disconnected, this, [this] { setState(State::Disconnected); });
    connect(m_worker, &SshWorker::errorOccurred, this, &Transport::errorOccurred);
    connect(m_worker, &SshWorker::dataReceived, this, &Transport::readyRead);

    m_thread.start();
}

SshTransport::~SshTransport()
{
    QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
    m_thread.quit();
    m_thread.wait(3000);
}

void SshTransport::connectToHost()
{
    setState(State::Connecting);
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection);
}

void SshTransport::disconnectFromHost()
{
    QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
}

void SshTransport::resize(int cols, int rows)
{
    QMetaObject::invokeMethod(m_worker, "requestResize", Qt::QueuedConnection, Q_ARG(int, cols), Q_ARG(int, rows));
}

void SshTransport::write(const QByteArray &data)
{
    QMetaObject::invokeMethod(m_worker, "enqueueWrite", Qt::QueuedConnection, Q_ARG(QByteArray, data));
}

bool SshTransport::confirmHostKey(const QString &message)
{
    return QMessageBox::warning(nullptr, QStringLiteral("SSH Host Key"), message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        == QMessageBox::Yes;
}
