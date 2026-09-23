#pragma once

#include <QByteArray>

// Wraps Windows DPAPI (CryptProtectData/CryptUnprotectData) for at-rest
// storage of saved-connection secrets (SSH passwords/passphrases).
//
// IMPORTANT CAVEAT, surfaced to the user in ConnectionEditDialog: DPAPI
// ties the encryption to the current Windows user account, so the
// resulting blob is useless if copied to another machine or read by
// another account - but it is *not* protection against malware or any
// other code running as this same user, which can call CryptUnprotectData
// itself just as easily as lvdterm does. This is "safe from someone
// browsing your files", not "safe from anything running on your PC".
namespace Crypto
{

// Returns an opaque encrypted blob suitable for storing in
// connections.json (base64-encode it first - see ConnectionProfile).
// Returns an empty QByteArray on failure (e.g. plaintext was empty).
QByteArray protect(const QByteArray &plaintext);

// Reverses protect(). Returns an empty QByteArray if decryption fails
// (e.g. the blob was produced by a different Windows user account, or
// is corrupt) - callers should treat that the same as "no saved secret"
// rather than as a hard error.
QByteArray unprotect(const QByteArray &encrypted);

} // namespace Crypto
