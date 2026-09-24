#include "SshTransport.h"

#include <QMessageBox>

#include "SshWorker.h"
#include "core/ThreadReaper.h"

SshTransport::SshTransport(SshConnectionSettings settings, QObject *parent)
    : Transport(parent)
    , m_settings(std::move(settings))
    , m_thread(new QThread)
    , m_worker(new SshWorker(m_settings, this))
{
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &SshWorker::connected, this, [this] { setState(State::Connected); });
    connect(m_worker, &SshWorker::disconnected, this, [this] { setState(State::Disconnected); });
    connect(m_worker, &SshWorker::errorOccurred, this, &Transport::errorOccurred);
    connect(m_worker, &SshWorker::dataReceived, this, &Transport::readyRead);

    m_thread->start();
}

SshTransport::~SshTransport()
{
    // Ask the worker to stop and hand the thread off to ThreadReaper
    // rather than blocking here with QThread::wait() - the worker may
    // still be stuck inside a plain blocking connect()/select() (see
    // SshConnectHelper::connectSocket()) for up to ~10-15s, and blocking
    // *this* (usually the GUI thread - a pane close or reconnect runs
    // this destructor directly) for that long is a frozen UI, not a
    // crash, but no better an experience. m_worker's own QPointer<
    // SshTransport> back-reference (see SshWorker) safely goes null if
    // it outlives this object, so it's safe to just walk away here.
    QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
    m_thread->quit();
    ThreadReaper::detach(m_thread);
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
