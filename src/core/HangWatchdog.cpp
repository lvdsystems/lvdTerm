#include "HangWatchdog.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <execinfo.h>
#include <pthread.h>
#include <unistd.h>
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>

#include "Version.h"

namespace
{
using Clock = std::chrono::steady_clock;

constexpr int kStallSeconds = 10;
constexpr qint64 kMaxLogBytes = 1024 * 1024;

std::thread g_thread;
std::atomic<bool> g_stop{false};
std::atomic<qint64> g_lastPongMs{0};
std::atomic<bool> g_pingPending{false};
QString g_logPath;

qint64 nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

FILE *openLog()
{
#ifdef _WIN32
    return _wfopen(g_logPath.toStdWString().c_str(), L"a");
#else
    return std::fopen(QFile::encodeName(g_logPath).constData(), "a");
#endif
}

#ifdef _WIN32
HANDLE g_guiThread = nullptr;

void writeGuiStack(FILE *log)
{
    constexpr int kMaxFrames = 64;
    DWORD64 frames[kMaxFrames];
    int count = 0;

    // Nothing here may allocate or take a lock the suspended thread could hold,
    // so addresses are only collected now and turned into text after it resumes.
    if (SuspendThread(g_guiThread) == static_cast<DWORD>(-1))
        return;
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(g_guiThread, &context)) {
        while (count < kMaxFrames && context.Rip) {
            frames[count++] = context.Rip;
            DWORD64 imageBase = 0;
            if (PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr)) {
                void *handlerData = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisher, nullptr);
            } else {
                // A leaf function: the return address is on top of the stack.
                context.Rip = *reinterpret_cast<DWORD64 *>(context.Rsp);
                context.Rsp += 8;
            }
        }
    }
    ResumeThread(g_guiThread);

    for (int i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        wchar_t path[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(frames[i]), &module))
            GetModuleFileNameW(module, path, MAX_PATH);
        const QString name = QFileInfo(QString::fromWCharArray(path)).fileName();
        std::fprintf(log, "  #%02d %s+0x%llx\n", i, name.toUtf8().constData(),
                     static_cast<unsigned long long>(frames[i] - reinterpret_cast<DWORD64>(module)));
    }
}
#else
pthread_t g_guiThread;
int g_stackFd = -1;

// Runs on the GUI thread itself, whatever it was blocked in.
void stackSignalHandler(int)
{
    void *frames[64];
    const int count = backtrace(frames, 64);
    backtrace_symbols_fd(frames, count, g_stackFd);
}

void writeGuiStack(FILE *log)
{
    std::fflush(log);
    g_stackFd = fileno(log);
    pthread_kill(g_guiThread, SIGUSR2);
    usleep(300 * 1000); // the handler has no way to say it finished
}
#endif

void ping()
{
    if (g_pingPending.exchange(true))
        return; // the previous one is still unanswered
    QMetaObject::invokeMethod(
        qApp, [] {
            g_lastPongMs = nowMs();
            g_pingPending = false;
        },
        Qt::QueuedConnection);
}

void run()
{
    qint64 lastTick = nowMs();
    qint64 stallStart = 0; // when the stall being reported began, 0 if none

    while (!g_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const qint64 now = nowMs();

        // This thread itself didn't run for a while: the machine slept, it
        // wasn't the GUI thread stalling.
        if (now - lastTick > 5000)
            g_lastPongMs = now;
        lastTick = now;

        ping();

        const qint64 silentMs = now - g_lastPongMs;
        if (!stallStart && silentMs >= kStallSeconds * 1000) {
            stallStart = g_lastPongMs;
            if (FILE *log = openLog()) {
                std::fprintf(log, "%s lvdterm %s: GUI thread not responding for %d s. Its call stack:\n",
                             QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8().constData(), LVDTERM_VERSION_STRING, kStallSeconds);
                writeGuiStack(log);
                std::fclose(log);
            }
        } else if (stallStart && silentMs < kStallSeconds * 1000) {
            if (FILE *log = openLog()) {
                std::fprintf(log, "%s GUI thread responding again after %lld s\n", QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8().constData(),
                             static_cast<long long>((g_lastPongMs - stallStart) / 1000));
                std::fclose(log);
            }
            stallStart = 0;
        }
    }
}
} // namespace

void HangWatchdog::start()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    g_logPath = dir + QStringLiteral("/hang.log");
    if (QFileInfo(g_logPath).size() > kMaxLogBytes)
        QFile::remove(g_logPath);

#ifdef _WIN32
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_guiThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
#else
    g_guiThread = pthread_self();
    struct sigaction action = {};
    action.sa_handler = stackSignalHandler;
    action.sa_flags = SA_RESTART; // whatever the GUI thread was in carries on afterwards
    sigemptyset(&action.sa_mask);
    sigaction(SIGUSR2, &action, nullptr);
    // glibc loads its unwinder on first use, which is not safe from a signal handler.
    void *frames[1];
    backtrace(frames, 1);
#endif

    g_lastPongMs = nowMs();
    g_thread = std::thread(run);
}

void HangWatchdog::stop()
{
    g_stop = true;
    if (g_thread.joinable())
        g_thread.join();
}
