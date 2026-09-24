#include "ThreadReaper.h"

#include <QElapsedTimer>
#include <QList>
#include <QThread>

namespace
{
QList<QThread *> &pendingThreads()
{
    static QList<QThread *> list;
    return list;
}
} // namespace

void ThreadReaper::detach(QThread *thread)
{
    pendingThreads().append(thread);

    // QThread::finished is emitted from the worker thread itself, so a
    // connection whose context is the QThread object (living on the GUI
    // thread, like whoever called detach()) is auto-upgraded to a queued
    // connection - this lambda runs later, back on the GUI thread, never
    // blocking anything in between.
    QObject::connect(thread, &QThread::finished, thread, [thread] {
        pendingThreads().removeOne(thread);
        thread->deleteLater();
    });
}

void ThreadReaper::waitForAll(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();

    // Snapshot the list up front: nothing here re-enters the event loop
    // to process a queued finished() while this loop runs, but iterating
    // a copy rather than the live list is one less thing to reason about
    // if that ever changes.
    const QList<QThread *> threads = pendingThreads();
    for (QThread *thread : threads) {
        const qint64 remaining = timeoutMs - timer.elapsed();
        if (remaining <= 0)
            break;
        thread->wait(remaining);
    }
}
