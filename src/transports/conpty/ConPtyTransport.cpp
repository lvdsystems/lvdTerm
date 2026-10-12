#include "ConPtyTransport.h"

#include <vector>

#include <windows.h>

#include <QMetaObject>

namespace
{
// Declared here instead of taken from <consoleapi.h>: not every
// toolchain's headers (or the _WIN32_WINNT they default to) expose them.
using LvdHPCON = void *;
using CreatePseudoConsoleFn = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD, LvdHPCON *);
using ResizePseudoConsoleFn = HRESULT(WINAPI *)(LvdHPCON, COORD);
using ClosePseudoConsoleFn = void(WINAPI *)(LvdHPCON);

constexpr DWORD_PTR kProcThreadAttributePseudoConsole = 0x00020016;

struct ConPtyApi
{
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn close = nullptr;

    bool available() const { return create && resize && close; }
};

const ConPtyApi &api()
{
    static const ConPtyApi instance = [] {
        ConPtyApi a;
        if (HMODULE kernel = GetModuleHandleW(L"kernel32.dll")) {
            a.create = reinterpret_cast<CreatePseudoConsoleFn>(reinterpret_cast<void *>(GetProcAddress(kernel, "CreatePseudoConsole")));
            a.resize = reinterpret_cast<ResizePseudoConsoleFn>(reinterpret_cast<void *>(GetProcAddress(kernel, "ResizePseudoConsole")));
            a.close = reinterpret_cast<ClosePseudoConsoleFn>(reinterpret_cast<void *>(GetProcAddress(kernel, "ClosePseudoConsole")));
        }
        return a;
    }();
    return instance;
}

COORD coordFor(int cols, int rows)
{
    COORD size;
    size.X = static_cast<SHORT>(cols);
    size.Y = static_cast<SHORT>(rows);
    return size;
}
} // namespace

ConPtyTransport::ConPtyTransport(QString shell, QObject *parent) : Transport(parent), m_shell(std::move(shell)) {}

ConPtyTransport::~ConPtyTransport()
{
    shutdown();
}

void ConPtyTransport::connectToHost()
{
    if (m_pseudoConsole)
        return;

    setState(State::Connecting);

    auto fail = [this](const QString &message) {
        emit errorOccurred(message);
        shutdown();
        setState(State::Error);
    };

    if (!api().available()) {
        fail(QStringLiteral("Local terminals need Windows 10 version 1809 or newer (no pseudo-console support found)"));
        return;
    }

    // Two pipes: the pseudo-console reads our keystrokes from one end and
    // writes its rendered output to the other; we keep the opposite ends.
    HANDLE inputRead = nullptr;
    HANDLE outputWrite = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite, nullptr, 0) || !CreatePipe(&outputRead, &outputWrite, nullptr, 0)) {
        if (inputRead) {
            CloseHandle(inputRead);
            CloseHandle(inputWrite);
        }
        fail(QStringLiteral("Could not create pipes for the local terminal"));
        return;
    }

    LvdHPCON pcon = nullptr;
    const HRESULT hr = api().create(coordFor(m_cols, m_rows), inputRead, outputWrite, 0, &pcon);
    // The pseudo-console duplicated what it needs; our copies of its ends must go.
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (FAILED(hr)) {
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        fail(QStringLiteral("CreatePseudoConsole failed (0x%1)").arg(static_cast<quint32>(hr), 8, 16, QLatin1Char('0')));
        return;
    }

    m_pseudoConsole = pcon;
    m_inputWrite = inputWrite;
    m_outputRead = outputRead;

    // Attribute list carrying the pseudo-console for CreateProcess.
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuffer(attrSize);
    auto *attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuffer.data());
    if (!InitializeProcThreadAttributeList(attrList, 1, 0, &attrSize)
        || !UpdateProcThreadAttribute(attrList, 0, kProcThreadAttributePseudoConsole, pcon, sizeof(pcon), nullptr, nullptr)) {
        fail(QStringLiteral("Could not attach the pseudo-console to the shell process"));
        return;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attrList;

    QString shell = m_shell.isEmpty() ? QStringLiteral("powershell.exe") : m_shell;
    std::wstring commandLine = shell.toStdWString(); // CreateProcessW may modify the buffer
    commandLine.push_back(L'\0');

    const QString home = qEnvironmentVariable("USERPROFILE");
    const std::wstring homeDir = home.toStdWString();

    PROCESS_INFORMATION info{};
    const BOOL ok = CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                   homeDir.empty() ? nullptr : homeDir.c_str(), &startup.StartupInfo, &info);
    DeleteProcThreadAttributeList(attrList);
    if (!ok) {
        fail(QStringLiteral("Could not start %1 (error %2)").arg(shell).arg(GetLastError()));
        return;
    }
    CloseHandle(info.hThread);
    m_process = info.hProcess;

    m_shuttingDown = false;
    m_reader = std::thread([this, handle = static_cast<HANDLE>(m_outputRead)] {
        char buffer[16384];
        DWORD n = 0;
        while (ReadFile(handle, buffer, sizeof(buffer), &n, nullptr) && n > 0) {
            const QByteArray chunk(buffer, static_cast<int>(n));
            QMetaObject::invokeMethod(this, [this, chunk] { emit readyRead(chunk); }, Qt::QueuedConnection);
        }
        // The pipe broke: the pseudo-console went away (shell exited) or
        // shutdown() closed it. Only the former needs reporting.
        if (!m_shuttingDown)
            QMetaObject::invokeMethod(this, [this] { onChildExited(); }, Qt::QueuedConnection);
    });

    setState(State::Connected);
}

void ConPtyTransport::disconnectFromHost()
{
    shutdown();
    setState(State::Disconnected);
}

void ConPtyTransport::resize(int cols, int rows)
{
    m_cols = cols;
    m_rows = rows;

    if (m_pseudoConsole && api().available())
        api().resize(m_pseudoConsole, coordFor(cols, rows));
}

void ConPtyTransport::write(const QByteArray &data)
{
    if (!m_inputWrite)
        return;

    // Synchronous: the pipe buffers ordinary typing; only a huge paste
    // into a shell that isn't reading could make this wait.
    const char *p = data.constData();
    qsizetype left = data.size();
    while (left > 0) {
        DWORD written = 0;
        if (!WriteFile(m_inputWrite, p, static_cast<DWORD>(left), &written, nullptr) || written == 0)
            return; // shell gone; the reader thread reports it
        p += written;
        left -= written;
    }
}

void ConPtyTransport::onChildExited()
{
    // A queued call can arrive after an explicit disconnect already ran.
    if (!m_pseudoConsole)
        return;

    shutdown();
    setState(State::Disconnected);
}

void ConPtyTransport::shutdown()
{
    m_shuttingDown = true;

    if (m_process) {
        // No-op if it already exited; otherwise stops a shell that
        // would outlive the tab.
        TerminateProcess(static_cast<HANDLE>(m_process), 0);
        CloseHandle(static_cast<HANDLE>(m_process));
        m_process = nullptr;
    }

    // Closing the pseudo-console ends the output pipe, which unblocks the
    // reader thread's ReadFile - so it must be joined only after this.
    // (On older Windows 10 this call can wait for the output to be
    // drained, which the still-running reader thread does.)
    if (m_pseudoConsole) {
        api().close(m_pseudoConsole);
        m_pseudoConsole = nullptr;
    }

    if (m_reader.joinable())
        m_reader.join();

    if (m_inputWrite) {
        CloseHandle(static_cast<HANDLE>(m_inputWrite));
        m_inputWrite = nullptr;
    }
    if (m_outputRead) {
        CloseHandle(static_cast<HANDLE>(m_outputRead));
        m_outputRead = nullptr;
    }
}
