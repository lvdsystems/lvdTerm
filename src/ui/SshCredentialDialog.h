#pragma once

#include "transports/ssh/SshConnectionSettings.h"

// Resolves SshConnectionSettings::promptForUsername/promptForPassword
// into concrete values by asking once, up front - before an SshTransport
// (the interactive pane) or SftpSession (the SFTP dock's independent
// second connection to the same host) is ever constructed from these
// settings. That timing is what makes it "ask once": both consumers are
// built from the same already-resolved copy afterward, so neither one
// separately triggers its own prompt deep inside its own connect
// sequence - asking twice for the one thing the user typed once was
// exactly the bug this fixes.
//
// GUI-thread only: shows modal dialogs directly. Never call this from a
// worker thread - see SshConnectHelper.h for the (different, blocking,
// worker-thread-only) helpers that run after this has already resolved
// settings for a given connection attempt.
namespace SshCredentialDialog
{

// Returns false if the user cancels either prompt - the caller should
// abort the connection attempt entirely rather than proceed with
// whatever settings ended up holding.
bool resolve(SshConnectionSettings &settings);

} // namespace SshCredentialDialog
