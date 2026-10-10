#pragma once

#include <QByteArray>

// At-rest encryption for saved-connection secrets (SSH passwords/
// passphrases), backed by whatever the OS offers for "tie this to the
// logged-in user, no UI prompt, never block": Windows DPAPI
// (CryptProtectData/CryptUnprotectData), or on Linux a libsecret-stored
// master key wrapping AES-256-GCM (see Crypto.cpp's Linux half for the
// full design and why).
//
// IMPORTANT CAVEAT, surfaced to the user in ConnectionEditDialog: both
// backends tie the encryption to the current OS user account, so the
// resulting blob is useless if copied to another machine or read by
// another account - but neither is protection against malware or any
// other code running as this same user, which can call the same OS APIs
// lvdterm does just as easily. This is "safe from someone browsing your
// files", not "safe from anything running on your PC".
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
