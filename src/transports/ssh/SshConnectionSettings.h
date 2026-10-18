#pragma once

#include <QMetaType>
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

    // When true, username above is ignored and the user is asked for one
    // at connect time instead (see SshCredentialDialog::resolve(),
    // called once per connect attempt before this struct ever reaches
    // SshConnectHelper::authenticate()) - a blank username saved in a
    // profile isn't a meaningful, intentional choice the way a blank
    // password sometimes is, so there's no separate "use literally
    // blank" state for this one.
    bool promptForUsername = false;

    AuthMethod authMethod = AuthMethod::Password;

    // AuthMethod::Password (and reused as the answer to any
    // keyboard-interactive prompt if publickey/password both fail to
    // authenticate outright - see SshConnectHelper::authenticate()).
    QString password;

    // When true (AuthMethod::Password only), password above is ignored
    // and the user is asked for one at connect time instead (see
    // SshCredentialDialog::resolve()), never saved. Deliberately
    // independent of password.isEmpty(): a connection can validly want
    // either a literal, saved *empty* password (some servers do allow
    // that) or a fresh prompt every time - collapsing "field left blank"
    // into "ask me" would make the former impossible to express.
    bool promptForPassword = false;

    // AuthMethod::PublicKey.
    QString privateKeyPath;
    QString passphrase;

    // Forward X11 to the local X server named by $DISPLAY (see
    // X11Forwarder). Harmless if the server refuses or no local X server
    // is running - the shell works either way.
    bool forwardX11 = true;
};

// The result of asking the user for a fresh password mid-connect, after
// a password auth attempt was rejected - see SshWorker::start()'s retry
// loop and SshTransport::promptForPassword(). accepted is false if they
// cancelled, which should give up on the connection attempt rather than
// retry with whatever was typed (or nothing at all).
struct SshCredentialPrompt
{
    bool accepted = false;
    QString value;
};
Q_DECLARE_METATYPE(SshCredentialPrompt)
