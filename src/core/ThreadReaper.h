#pragma once

class QThread;

// A place for a QThread-owning class to hand off a worker thread that's
// being asked to quit, without blocking the calling thread until it
// actually does. Blocking (QThread::wait()) on the GUI thread is exactly
// how a stuck worker (e.g. still inside a plain blocking connect() with
// no timeout reached yet) turns into a frozen UI instead of a quick,
// responsive teardown - see SshTransport/SftpSession, whose destructors
// used to do exactly this.
//
// detach() takes ownership of a QThread that has already been told to
// quit (quit()/a stop request already sent to its worker) and deletes it
// once QThread::finished actually fires, whenever that happens to be -
// the calling thread never waits. The one place a bounded, blocking wait
// is still needed is application shutdown, where QApplication (and
// anything it or a library like libssh2 assumes is torn down) must not
// go away while a detached thread might still be running - waitForAll()
// is that one exception, meant to be called exactly once, late in
// main()'s shutdown sequence.
class ThreadReaper
{
public:
    // thread must already have had quit() (or an equivalent stop
    // request) issued, and must not have a QObject parent - ThreadReaper
    // becomes its sole owner and deletes it once it finishes.
    static void detach(QThread *thread);

    // Waits, bounded by timeoutMs in total (not per thread), for every
    // still-running detached thread. Safe to call even if some/all have
    // already finished (or none were ever detached).
    static void waitForAll(int timeoutMs);
};
