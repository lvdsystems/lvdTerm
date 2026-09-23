#pragma once

#include <QString>

// Plain value type describing how to open an SSH connection. Mirrors
// SerialPortSettings/the telnet host+port pair: doubles as the persisted
// profile payload for this connection type (see ConnectionProfile).
struct SshConnectionSettings
{
    enum class AuthMethod
    {
        Password,
        PublicKey,
    };

    QString host;
    quint16 port = 22;
    QString username;
    AuthMethod authMethod = AuthMethod::Password;

    // AuthMethod::Password (and reused as the answer to any
    // keyboard-interactive prompt if publickey/password both fail to
    // authenticate outright - see SshWorker::authenticate).
    QString password;

    // AuthMethod::PublicKey.
    QString privateKeyPath;
    QString passphrase;
};
