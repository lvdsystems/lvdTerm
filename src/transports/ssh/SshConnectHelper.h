#pragma once

#include <libssh2.h>

#include <QString>

#include "SshConnectionSettings.h"

// The blocking connect/verify/auth sequence shared by SshWorker (the
// interactive terminal session) and SftpClient (the file-browser dock's
// second session to the same host). Keeping this in one place matters
// beyond not repeating code: both sessions must
// agree on what "trusted" means (the same known_hosts store, the same
// prompt) or a host accepted for one could still surprise-prompt for the
// other.
//
// Every function here is blocking and meant to be called from a
// dedicated worker thread, never the GUI thread.
namespace SshConnectHelper
{

// Where lvdterm keeps its own known_hosts file, independent of any
// OpenSSH installation the user might have.
QString knownHostsPath();

// Resolves and connects a plain TCP socket to host:port. Returns
// LIBSSH2_INVALID_SOCKET on failure with *error set.
libssh2_socket_t connectSocket(const QString &host, quint16 port, QString *error);

// Performs the SSH handshake on an already-connected socket and checks
// the presented host key against knownHostsPath(). If the key is new or
// changed, invokes promptTarget's Q_INVOKABLE
// "bool confirmHostKey(QString)" via a *blocking* queued call, so
// promptTarget must live on the GUI thread while this runs on a worker
// thread. On acceptance of a new/changed key, updates the known_hosts
// file. Returns false on failure or if the user rejects the key, with
// *error set.
bool handshakeAndVerifyHostKey(LIBSSH2_SESSION *session, libssh2_socket_t socket, const QString &host, quint16 port, QObject *promptTarget,
                                QString *error);

// Tries password or public-key auth per settings.authMethod, then falls
// back to a password-style keyboard-interactive prompt if that's what
// the server actually wants and a password is available (no 2FA/OTP
// support). Returns false on failure with *error set.
bool authenticate(LIBSSH2_SESSION *session, const SshConnectionSettings &settings, QString *error);

} // namespace SshConnectHelper
