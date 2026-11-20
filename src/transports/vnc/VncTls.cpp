#include "VncTls.h"

#include <QHostAddress>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace
{
X509 *peerCertificate(SSL *ssl)
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    return SSL_get1_peer_certificate(ssl);
#else
    return SSL_get_peer_certificate(ssl);
#endif
}

QString nameLine(X509_NAME *name)
{
    if (!name)
        return QString();
    char buf[512];
    X509_NAME_oneline(name, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}
} // namespace

VncTls::~VncTls()
{
    if (m_ssl)
        SSL_free(m_ssl); // also frees m_in/m_out
    if (m_ctx)
        SSL_CTX_free(m_ctx);
}

QString VncTls::lastError(const char *what)
{
    const unsigned long code = ERR_get_error();
    ERR_clear_error();
    if (code == 0)
        return QStringLiteral("TLS: %1 failed").arg(QLatin1String(what));
    char buf[256];
    ERR_error_string_n(code, buf, sizeof(buf));
    return QStringLiteral("TLS: %1 failed (%2)").arg(QLatin1String(what), QString::fromUtf8(buf));
}

bool VncTls::start(Mode mode, const QString &serverName, QString *error)
{
    ERR_clear_error();
    m_ctx = SSL_CTX_new(TLS_client_method());
    if (!m_ctx) {
        *error = lastError("setup");
        return false;
    }

    if (mode == Mode::Anonymous) {
        // Anonymous (EC)DH: no certificates, so no TLS 1.3 (which has no
        // anonymous suites), and OpenSSL only allows these at security
        // level 0. Servers using this (e.g. GNOME's vino) can be old, so
        // TLS 1.0 stays allowed here too.
        SSL_CTX_set_security_level(m_ctx, 0);
        SSL_CTX_set_min_proto_version(m_ctx, TLS1_VERSION);
        SSL_CTX_set_max_proto_version(m_ctx, TLS1_2_VERSION);
        if (SSL_CTX_set_cipher_list(m_ctx, "aNULL:!eNULL:@SECLEVEL=0") != 1) {
            *error = lastError("selecting anonymous ciphers");
            return false;
        }
    } else {
        // The certificate is checked by fingerprint afterwards (VncClient),
        // not against a CA: VNC servers almost always have self-signed ones.
        SSL_CTX_set_verify(m_ctx, SSL_VERIFY_NONE, nullptr);
    }

    m_ssl = SSL_new(m_ctx);
    m_in = BIO_new(BIO_s_mem());
    m_out = BIO_new(BIO_s_mem());
    if (!m_ssl || !m_in || !m_out) {
        if (m_in && !m_ssl)
            BIO_free(m_in);
        if (m_out && !m_ssl)
            BIO_free(m_out);
        *error = lastError("setup");
        return false;
    }
    SSL_set_bio(m_ssl, m_in, m_out);
    SSL_set_connect_state(m_ssl);

    // SNI only for names, never for an IP address (RFC 6066).
    if (mode == Mode::X509 && !serverName.isEmpty() && QHostAddress(serverName).isNull())
        SSL_set_tlsext_host_name(m_ssl, serverName.toUtf8().constData());

    return pump(nullptr, error);
}

bool VncTls::pump(QByteArray *plain, QString *error)
{
    if (!m_handshakeDone) {
        const int rc = SSL_do_handshake(m_ssl);
        if (rc == 1) {
            m_handshakeDone = true;
            if (!flushPending(error))
                return false;
        } else {
            const int err = SSL_get_error(m_ssl, rc);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
                return true;
            *error = lastError("handshake");
            return false;
        }
    }

    if (!plain)
        return true;
    char buf[16384];
    for (;;) {
        const int n = SSL_read(m_ssl, buf, sizeof(buf));
        if (n > 0) {
            plain->append(buf, n);
            continue;
        }
        const int err = SSL_get_error(m_ssl, n);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_ZERO_RETURN)
            return true; // need more input / the server closed TLS (the socket closing follows)
        *error = lastError("decrypting");
        return false;
    }
}

bool VncTls::feed(const QByteArray &cipher, QByteArray *plain, QString *error)
{
    if (!m_ssl)
        return false;
    if (!cipher.isEmpty() && BIO_write(m_in, cipher.constData(), static_cast<int>(cipher.size())) <= 0) {
        *error = lastError("buffering");
        return false;
    }
    return pump(plain, error);
}

bool VncTls::write(const QByteArray &plain, QString *error)
{
    if (!m_ssl)
        return false;
    m_pendingWrite.append(plain);
    return m_handshakeDone ? flushPending(error) : true;
}

bool VncTls::flushPending(QString *error)
{
    while (!m_pendingWrite.isEmpty()) {
        const int n = SSL_write(m_ssl, m_pendingWrite.constData(), static_cast<int>(m_pendingWrite.size()));
        if (n <= 0) {
            *error = lastError("encrypting");
            return false;
        }
        m_pendingWrite.remove(0, n);
    }
    return true;
}

QByteArray VncTls::takeOutgoing()
{
    QByteArray out;
    if (!m_out)
        return out;
    char buf[16384];
    while (BIO_ctrl_pending(m_out) > 0) {
        const int n = BIO_read(m_out, buf, sizeof(buf));
        if (n <= 0)
            break;
        out.append(buf, n);
    }
    return out;
}

QString VncTls::peerFingerprint() const
{
    X509 *cert = m_ssl ? peerCertificate(m_ssl) : nullptr;
    if (!cert)
        return QString();
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    QString result;
    if (X509_digest(cert, EVP_sha256(), md, &len) == 1)
        result = QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(md), static_cast<int>(len)).toHex(':').toUpper());
    X509_free(cert);
    return result;
}

QString VncTls::peerSubject() const
{
    X509 *cert = m_ssl ? peerCertificate(m_ssl) : nullptr;
    if (!cert)
        return QString();
    const QString s = nameLine(X509_get_subject_name(cert));
    X509_free(cert);
    return s;
}

QString VncTls::peerIssuer() const
{
    X509 *cert = m_ssl ? peerCertificate(m_ssl) : nullptr;
    if (!cert)
        return QString();
    const QString s = nameLine(X509_get_issuer_name(cert));
    X509_free(cert);
    return s;
}
