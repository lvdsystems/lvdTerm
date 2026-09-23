#include "SshConnectHelper.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <QDir>
#include <QMetaObject>
#include <QStandardPaths>

namespace
{
QString hostKeyTypeMask(int libssh2HostkeyType, int *outKnownhostKeyBit)
{
    switch (libssh2HostkeyType) {
    case LIBSSH2_HOSTKEY_TYPE_RSA:
        *outKnownhostKeyBit = LIBSSH2_KNOWNHOST_KEY_SSHRSA;
        return QStringLiteral("RSA");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
        *outKnownhostKeyBit = LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
        return QStringLiteral("ECDSA-256");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
        *outKnownhostKeyBit = LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
        return QStringLiteral("ECDSA-384");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
        *outKnownhostKeyBit = LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
        return QStringLiteral("ECDSA-521");
    case LIBSSH2_HOSTKEY_TYPE_ED25519:
        *outKnownhostKeyBit = LIBSSH2_KNOWNHOST_KEY_ED25519;
        return QStringLiteral("ED25519");
    default:
        *outKnownhostKeyBit = LIBSSH2_KNOWNHOST_KEY_UNKNOWN;
        return QStringLiteral("unknown");
    }
}

void keyboardInteractiveCallback(const char * /*name*/, int /*name_len*/, const char * /*instruction*/, int /*instruction_len*/,
                                  int num_prompts, const LIBSSH2_USERAUTH_KBDINT_PROMPT * /*prompts*/,
                                  LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses, void **abstract)
{
    const auto *password = static_cast<const QString *>(*abstract);
    const QByteArray passwordUtf8 = password->toUtf8();

    for (int i = 0; i < num_prompts; ++i) {
        responses[i].text = static_cast<char *>(malloc(static_cast<size_t>(passwordUtf8.size())));
        if (responses[i].text) {
            memcpy(responses[i].text, passwordUtf8.constData(), static_cast<size_t>(passwordUtf8.size()));
            responses[i].length = static_cast<unsigned int>(passwordUtf8.size());
        } else {
            responses[i].length = 0;
        }
    }
}
} // namespace

namespace SshConnectHelper
{

QString knownHostsPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/known_hosts");
}

libssh2_socket_t connectSocket(const QString &host, quint16 port, QString *error)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        if (error)
            *error = QStringLiteral("WSAStartup failed");
        return LIBSSH2_INVALID_SOCKET;
    }

    const libssh2_socket_t sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == LIBSSH2_INVALID_SOCKET) {
        if (error)
            *error = QStringLiteral("Could not create socket");
        return LIBSSH2_INVALID_SOCKET;
    }

    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *result = nullptr;
    const QByteArray hostUtf8 = host.toUtf8();
    const QByteArray portUtf8 = QByteArray::number(port);
    if (getaddrinfo(hostUtf8.constData(), portUtf8.constData(), &hints, &result) != 0 || !result) {
        if (error)
            *error = QStringLiteral("Could not resolve host: %1").arg(host);
        closesocket(sock);
        return LIBSSH2_INVALID_SOCKET;
    }

    // Qualified: a class using this helper is typically a QObject, whose
    // own connect() would otherwise shadow winsock's ::connect() here.
    const int rc = ::connect(sock, result->ai_addr, static_cast<int>(result->ai_addrlen));
    freeaddrinfo(result);

    if (rc != 0) {
        if (error)
            *error = QStringLiteral("Could not connect to %1:%2").arg(host).arg(port);
        closesocket(sock);
        return LIBSSH2_INVALID_SOCKET;
    }

    return sock;
}

bool handshakeAndVerifyHostKey(LIBSSH2_SESSION *session, libssh2_socket_t socket, const QString &host, quint16 port, QObject *promptTarget,
                                QString *error)
{
    if (libssh2_session_handshake(session, socket) != 0) {
        char *errmsg = nullptr;
        libssh2_session_last_error(session, &errmsg, nullptr, 0);
        if (error)
            *error = QStringLiteral("SSH handshake failed: %1").arg(QString::fromUtf8(errmsg ? errmsg : "unknown error"));
        return false;
    }

    size_t keyLen = 0;
    int keyType = 0;
    const char *keyBlob = libssh2_session_hostkey(session, &keyLen, &keyType);
    if (!keyBlob) {
        if (error)
            *error = QStringLiteral("Server did not present a host key");
        return false;
    }

    LIBSSH2_KNOWNHOSTS *hosts = libssh2_knownhost_init(session);
    if (hosts)
        libssh2_knownhost_readfile(hosts, knownHostsPath().toUtf8().constData(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);

    int knownhostKeyBit = LIBSSH2_KNOWNHOST_KEY_UNKNOWN;
    const QString keyTypeName = hostKeyTypeMask(keyType, &knownhostKeyBit);

    struct libssh2_knownhost *knownEntry = nullptr;
    const int checkResult = hosts
        ? libssh2_knownhost_checkp(hosts, host.toUtf8().constData(), port, keyBlob, keyLen,
                                   LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | knownhostKeyBit, &knownEntry)
        : LIBSSH2_KNOWNHOST_CHECK_FAILURE;

    if (checkResult != LIBSSH2_KNOWNHOST_CHECK_MATCH) {
        const char *fpRaw = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256);
        QString fingerprint = fpRaw
            ? QStringLiteral("SHA256:%1").arg(QString::fromLatin1(QByteArray(fpRaw, 32).toBase64(QByteArray::OmitTrailingEquals)))
            : QStringLiteral("(unavailable)");

        QString message;
        if (checkResult == LIBSSH2_KNOWNHOST_CHECK_MISMATCH) {
            message = QStringLiteral(
                          "WARNING: the %1 host key for %2 does NOT match the one on record.\n"
                          "This could mean someone is intercepting the connection.\n\n"
                          "Fingerprint: %3\n\nConnect anyway and update the saved key?")
                          .arg(keyTypeName, host, fingerprint);
        } else {
            message = QStringLiteral(
                          "The authenticity of host %1 (%2 key) can't be established.\n\n"
                          "Fingerprint: %3\n\nTrust this host and continue?")
                          .arg(host, keyTypeName, fingerprint);
        }

        bool accepted = false;
        QMetaObject::invokeMethod(promptTarget, "confirmHostKey", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, accepted),
                                   Q_ARG(QString, message));

        if (!accepted) {
            if (hosts)
                libssh2_knownhost_free(hosts);
            if (error)
                *error = QStringLiteral("Host key not accepted");
            return false;
        }

        if (hosts) {
            if (knownEntry)
                libssh2_knownhost_del(hosts, knownEntry);
            libssh2_knownhost_addc(hosts, host.toUtf8().constData(), nullptr, keyBlob, keyLen, nullptr, 0,
                                    LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | knownhostKeyBit, nullptr);
            libssh2_knownhost_writefile(hosts, knownHostsPath().toUtf8().constData(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        }
    }

    if (hosts)
        libssh2_knownhost_free(hosts);

    return true;
}

bool authenticate(LIBSSH2_SESSION *session, const SshConnectionSettings &settings, QString *error)
{
    const QByteArray user = settings.username.toUtf8();

    bool ok = false;
    if (settings.authMethod == SshConnectionSettings::AuthMethod::PublicKey) {
        ok = libssh2_userauth_publickey_fromfile(session, user.constData(), nullptr, settings.privateKeyPath.toUtf8().constData(),
                                                  settings.passphrase.toUtf8().constData())
            == 0;
    } else {
        ok = libssh2_userauth_password(session, user.constData(), settings.password.toUtf8().constData()) == 0;
    }

    // Common in practice: servers with password auth disabled but a
    // password-only keyboard-interactive prompt enabled instead. If we
    // have a password on hand, it's worth one more try before giving up.
    // (keyboardInteractiveCallback() cannot handle anything beyond
    // simple password-style prompts - no OTP/2FA support.)
    if (!ok && !settings.password.isEmpty()) {
        *libssh2_session_abstract(session) = const_cast<QString *>(&settings.password);
        ok = libssh2_userauth_keyboard_interactive(session, user.constData(), &keyboardInteractiveCallback) == 0;
    }

    if (!ok) {
        char *errmsg = nullptr;
        libssh2_session_last_error(session, &errmsg, nullptr, 0);
        if (error)
            *error = QStringLiteral("Authentication failed: %1").arg(QString::fromUtf8(errmsg ? errmsg : "unknown error"));
    }

    return ok;
}

} // namespace SshConnectHelper
