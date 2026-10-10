#ifndef _WIN32
// Must come before any Qt header is included anywhere in this file
// (including transitively via "Crypto.h" right below): Qt's signals/
// slots/emit macros and glib/gio's own use of "signals" as a plain
// struct member name (pulled in transitively by libsecret/secret.h,
// used only on this platform) collide, and Qt only guards those macros
// behind #ifndef QT_NO_KEYWORDS - this file never declares a QObject or
// uses signals/slots itself, so disabling them here is free.
#define QT_NO_KEYWORDS
#endif

#include "Crypto.h"

#ifdef _WIN32

#include <windows.h>

#include <dpapi.h>

namespace Crypto
{

QByteArray protect(const QByteArray &plaintext)
{
    if (plaintext.isEmpty())
        return {};

    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plaintext.constData()));
    in.cbData = static_cast<DWORD>(plaintext.size());

    DATA_BLOB out = {};
    const BOOL ok = CryptProtectData(&in, L"lvdterm saved connection secret", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok)
        return {};

    QByteArray result(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return result;
}

QByteArray unprotect(const QByteArray &encrypted)
{
    if (encrypted.isEmpty())
        return {};

    DATA_BLOB in;
    in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData()));
    in.cbData = static_cast<DWORD>(encrypted.size());

    DATA_BLOB out = {};
    const BOOL ok = CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    if (!ok)
        return {};

    QByteArray result(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return result;
}

} // namespace Crypto

#else // Linux

#include <chrono>
#include <thread>

#include <libsecret/secret.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

// Linux has no DPAPI equivalent that hands out a ready-to-use per-user
// key the way Windows does, so this builds the same property out of two
// pieces: libsecret (the freedesktop.org Secret Service - GNOME Keyring/
// KWallet underneath) stores exactly ONE random master key, lazily
// created on first use; that key then encrypts/decrypts the actual
// variable-length secret blobs via AES-256-GCM through OpenSSL (already
// a dependency here, for libssh2). One keyring item per saved connection
// was deliberately rejected: it would mean deleting/renaming a
// connection needs its own keyring-cleanup code, something DPAPI's
// self-contained blob-in-JSON never needed.
//
// Rejected alternatives, and why: qt6keychain would be cross-platform
// including Windows, but its Windows backend is Credential Manager, not
// raw DPAPI blobs - adopting it would change the Windows persistence
// model itself, exactly the regression this file exists to avoid.
// Deriving a key from /etc/machine-id would dodge the "no keyring
// daemon running" case below, but that file is commonly world-readable -
// a real security downgrade from DPAPI's user-login-tied key, not an
// equivalent fallback.
//
// Every failure path here (no Secret Service running - plausible for a
// terminal app launched headless/over SSH; the item missing; the GCM
// tag failing to verify, e.g. because the keyring was cleared and a
// *new* master key just got lazily created) soft-fails to an empty
// QByteArray, exactly like a DPAPI failure does - "this secret just
// doesn't decrypt," never a crash.
//
// One real finding from actually testing this against a locked default
// collection (not just reasoning about it): libsecret's "simple" sync
// API can block *indefinitely* if the collection needs an interactive
// unlock prompt that nothing is there to service - confirmed by hand,
// not hypothetical. Crypto::protect()/unprotect() run on the GUI thread
// (ConnectionProfile::toJson()/fromJson(), called when loading/saving
// profiles), so an unbounded hang here means a frozen app, a much worse
// failure than the "soft-fail to empty" this file is built around.
// masterKeyWithTimeout() below bounds every libsecret call with a
// GCancellable a watchdog thread cancels after a few seconds - the
// *supported* way to bound libsecret's sync calls (the simple API takes
// no timeout parameter of its own) - so a stuck prompt degrades to the
// same empty-QByteArray soft-fail as every other failure mode here,
// instead of hanging.
namespace Crypto
{
namespace
{

const SecretSchema *masterKeySchema()
{
    static const SecretSchema schema{"it.lvdsystems.lvdterm.MasterKey",
                                      SECRET_SCHEMA_NONE,
                                      {{"purpose", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SecretSchemaAttributeType(0)}}};
    return &schema;
}

constexpr int kKeyringTimeoutSeconds = 5;

// Runs fn(cancellable) with a GCancellable that a detached watchdog
// thread cancels after kKeyringTimeoutSeconds - bounding an otherwise-
// unbounded libsecret sync call (see the namespace comment above). The
// extra ref keeps the GCancellable alive for the watchdog even if fn()
// already returned and this function's own ref was dropped; cancelling
// an already-finished GCancellable that nothing is still listening to is
// harmless, just a no-op flag set on an otherwise-idle object.
template<typename Fn>
auto withKeyringTimeout(Fn &&fn) -> decltype(fn(static_cast<GCancellable *>(nullptr)))
{
    GCancellable *cancellable = g_cancellable_new();
    g_object_ref(cancellable);
    std::thread watchdog([cancellable] {
        std::this_thread::sleep_for(std::chrono::seconds(kKeyringTimeoutSeconds));
        g_cancellable_cancel(cancellable);
        g_object_unref(cancellable);
    });
    watchdog.detach();

    auto result = fn(cancellable);
    g_object_unref(cancellable);
    return result;
}

// Looks up the one master key lvdterm keeps in the user's keyring,
// creating it (32 random bytes, stored base64-encoded since libsecret's
// simple API stores strings) if it isn't there yet. Returns an empty
// QByteArray if the Secret Service can't be reached, times out, or the
// key can't be stored - callers treat that as "no secret available,"
// same as any other failure here.
//
// Cached in-process once successfully retrieved/created: there's no
// reason to re-hit the keyring (and re-pay the worst-case timeout above)
// for every single secret in a profile, or every profile at startup.
// Deliberately NOT caching a *failure*, though - a later call (e.g.
// after the user unlocks their keyring through some other means mid-
// session) still gets its own fresh attempt rather than being stuck with
// the first failure for the rest of the process's life.
QByteArray masterKey()
{
    static QByteArray cachedKey;
    if (!cachedKey.isEmpty())
        return cachedKey;

    const QByteArray found = withKeyringTimeout([](GCancellable *cancellable) -> QByteArray {
        GError *error = nullptr;
        gchar *stored = secret_password_lookup_sync(masterKeySchema(), cancellable, &error, "purpose", "master-key", nullptr);
        if (error) {
            g_error_free(error);
            error = nullptr;
        }
        if (!stored)
            return {};
        const QByteArray key = QByteArray::fromBase64(QByteArray(stored));
        secret_password_free(stored);
        return key;
    });
    // Wrong size (corrupt entry, or leftover from some other app using
    // the same schema by coincidence) falls through and regenerates
    // rather than using a bad key, same as an outright lookup failure.
    if (found.size() == 32) {
        cachedKey = found;
        return cachedKey;
    }

    QByteArray newKey(32, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(newKey.data()), newKey.size()) != 1)
        return {};

    const QByteArray encoded = newKey.toBase64();
    const bool storedOk = withKeyringTimeout([&encoded](GCancellable *cancellable) {
        GError *error = nullptr;
        const gboolean ok = secret_password_store_sync(masterKeySchema(), SECRET_COLLECTION_DEFAULT, "lvdterm saved connection secret",
                                                         encoded.constData(), cancellable, &error, "purpose", "master-key", nullptr);
        if (error) {
            g_error_free(error);
            error = nullptr;
        }
        return ok == TRUE;
    });
    if (!storedOk)
        return {};

    cachedKey = newKey;
    return cachedKey;
}

constexpr int kIvSize = 12;
constexpr int kTagSize = 16;

// Self-describing blob, mirroring DPAPI's own "one opaque blob" shape:
// iv || ciphertext || tag.
QByteArray aesGcmEncrypt(const QByteArray &key, const QByteArray &plaintext)
{
    QByteArray iv(kIvSize, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(iv.data()), iv.size()) != 1)
        return {};

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};

    QByteArray result;
    QByteArray ciphertext(plaintext.size() + EVP_CIPHER_block_size(EVP_aes_256_gcm()), Qt::Uninitialized);
    int len = 0;
    int ciphertextLen = 0;

    const bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) == 1
        && EVP_EncryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char *>(key.constData()),
                               reinterpret_cast<const unsigned char *>(iv.constData()))
            == 1
        && EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(ciphertext.data()), &len,
                              reinterpret_cast<const unsigned char *>(plaintext.constData()), plaintext.size())
            == 1;

    if (ok) {
        ciphertextLen = len;
        int finalLen = 0;
        if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(ciphertext.data()) + ciphertextLen, &finalLen) == 1) {
            ciphertextLen += finalLen;
            ciphertext.resize(ciphertextLen);

            QByteArray tag(kTagSize, Qt::Uninitialized);
            if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagSize, tag.data()) == 1)
                result = iv + ciphertext + tag;
        }
    }

    EVP_CIPHER_CTX_free(ctx);
    return result;
}

QByteArray aesGcmDecrypt(const QByteArray &key, const QByteArray &blob)
{
    if (blob.size() < kIvSize + kTagSize)
        return {};

    const QByteArray iv = blob.left(kIvSize);
    const QByteArray tag = blob.right(kTagSize);
    const QByteArray ciphertext = blob.mid(kIvSize, blob.size() - kIvSize - kTagSize);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};

    QByteArray plaintext(ciphertext.size(), Qt::Uninitialized);
    int len = 0;
    int plaintextLen = 0;

    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv.size(), nullptr) == 1
        && EVP_DecryptInit_ex(ctx, nullptr, nullptr, reinterpret_cast<const unsigned char *>(key.constData()),
                               reinterpret_cast<const unsigned char *>(iv.constData()))
            == 1
        && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(plaintext.data()), &len,
                              reinterpret_cast<const unsigned char *>(ciphertext.constData()), ciphertext.size())
            == 1;

    if (ok) {
        plaintextLen = len;
        ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagSize, const_cast<char *>(tag.constData())) == 1;
    }

    QByteArray result;
    if (ok) {
        int finalLen = 0;
        // A non-positive return here means the tag didn't verify - the
        // key is wrong (keyring was cleared and regenerated) or the blob
        // was tampered with/corrupted. Either way: no plaintext, soft
        // fail, same as every other error path in this file.
        if (EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(plaintext.data()) + plaintextLen, &finalLen) == 1) {
            plaintextLen += finalLen;
            result = plaintext.left(plaintextLen);
        }
    }

    EVP_CIPHER_CTX_free(ctx);
    return result;
}

} // namespace

QByteArray protect(const QByteArray &plaintext)
{
    if (plaintext.isEmpty())
        return {};
    const QByteArray key = masterKey();
    if (key.isEmpty())
        return {};
    return aesGcmEncrypt(key, plaintext);
}

QByteArray unprotect(const QByteArray &encrypted)
{
    if (encrypted.isEmpty())
        return {};
    const QByteArray key = masterKey();
    if (key.isEmpty())
        return {};
    return aesGcmDecrypt(key, encrypted);
}

} // namespace Crypto

#endif
