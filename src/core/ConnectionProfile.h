#pragma once

#include <QJsonObject>
#include <QString>
#include <QUuid>

#include "transports/serial/SerialPortSettings.h"
#include "transports/ssh/SshConnectionSettings.h"

// A saved connection: a name, a place in the folder tree, a type, and
// that type's settings. Only the member matching `type` is meaningful;
// the others sit at their default value. Persisted by ProfileStore as
// JSON (~/AppData/.../lvdterm/connections.json) with secrets (SSH
// password/passphrase) DPAPI-encrypted - see Crypto.h.
struct ConnectionProfile
{
    enum class Type
    {
        Serial,
        Telnet,
        Ssh,
    };

    QUuid id = QUuid::createUuid();
    QString name;
    QString folder; // "/"-separated path, e.g. "Work/Servers"; "" = root
    Type type = Type::Serial;
    bool autoReconnect = true;
    // See KeyboardProfiles.h - one of KeyboardProfiles::names(), or empty
    // to use AppSettings::defaultKeyboardProfile(). Applies regardless of
    // type, so it lives here rather than nested under serial/telnet/ssh.
    QString keyboardProfile;

    // Session logging - auto-starts TerminalView::startLogging(logFilePath)
    // on every connect when enabled, rather than needing the ad hoc
    // "Log Session to File..." action each time. logIncludeTimestamps
    // (a "HH:mm:ss.zzz " prefix per line - see TerminalView::
    // onDataForLogging()) is deliberately only ever set here, per saved
    // connection - not a global AppSettings default, and not offered on
    // the ad hoc quick-log action.
    bool logSessionToFile = false;
    QString logFilePath;
    bool logIncludeTimestamps = false;

    // What TerminalView shows the incoming bytes as. Hex bypasses the
    // vendored VT100 emulation entirely (see TerminalView/HexDumpWidget) -
    // useful for a serial/raw connection carrying a binary protocol
    // rather than an actual shell, where feeding random binary through
    // the terminal parser would just be noise (or worse). Applies
    // regardless of type, same as keyboardProfile above.
    enum class Viewer
    {
        Terminal,
        Hex,
    };
    Viewer viewer = Viewer::Terminal;

    SerialPortSettings serial;

    QString telnetHost;
    quint16 telnetPort = 23;
    bool rawMode = false; // Telnet type only: plain TCP, no IAC negotiation - see RawTransport

    SshConnectionSettings ssh;

    QJsonObject toJson() const;
    static ConnectionProfile fromJson(const QJsonObject &object);
};
