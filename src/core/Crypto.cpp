#include "Crypto.h"

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
