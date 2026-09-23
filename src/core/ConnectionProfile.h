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

    SerialPortSettings serial;

    QString telnetHost;
    quint16 telnetPort = 23;
    bool rawMode = false; // Telnet type only: plain TCP, no IAC negotiation - see RawTransport

    SshConnectionSettings ssh;

    QJsonObject toJson() const;
    static ConnectionProfile fromJson(const QJsonObject &object);
};
