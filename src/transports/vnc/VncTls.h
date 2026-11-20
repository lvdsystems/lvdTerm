#pragma once

#include <QByteArray>
#include <QString>

typedef struct ssl_ctx_st SSL_CTX;
typedef struct ssl_st SSL;
typedef struct bio_st BIO;

// A TLS client session run over memory buffers, for VncClient: the RFB
// handshake starts in the clear and switches to TLS part way through
// (security type 18 "TLS", and VeNCrypt's TLS and X509 sub-types), so the
// socket stays a plain QTcpSocket and this sits between it and the
// protocol code. OpenSSL, the same library already linked for the VNC
// password's DES and for FreeRDP.
//
// Anonymous mode is anonymous Diffie-Hellman (no certificate - what
// type 18 and VeNCrypt's TLS* sub-types use): encrypted, but not
// protected against a man in the middle. X509 mode takes the server's
// certificate; VncClient checks its fingerprint (trust on first use).
class VncTls
{
public:
    enum class Mode
    {
        Anonymous,
        X509,
    };

    VncTls() = default;
    ~VncTls();
    VncTls(const VncTls &) = delete;
    VncTls &operator=(const VncTls &) = delete;

    // Begins the handshake: the ClientHello is then waiting in takeOutgoing().
    bool start(Mode mode, const QString &serverName, QString *error);

    // Bytes received from the server. Decrypted data is appended to *plain.
    // False: the session failed (*error says why).
    bool feed(const QByteArray &cipher, QByteArray *plain, QString *error);

    // Data for the server: encrypted, then waiting in takeOutgoing().
    // Written before the handshake completes, it is held until then.
    bool write(const QByteArray &plain, QString *error);

    // Encrypted bytes to send on the socket.
    QByteArray takeOutgoing();

    bool handshakeDone() const { return m_handshakeDone; }

    // X509 mode, after the handshake: the server certificate's SHA-256
    // fingerprint ("AB:CD:..."), and its subject and issuer for display.
    QString peerFingerprint() const;
    QString peerSubject() const;
    QString peerIssuer() const;

private:
    bool pump(QByteArray *plain, QString *error); // advances the handshake / reads what can be decrypted
    bool flushPending(QString *error);
    static QString lastError(const char *what);

    SSL_CTX *m_ctx = nullptr;
    SSL *m_ssl = nullptr;
    BIO *m_in = nullptr;  // owned by m_ssl
    BIO *m_out = nullptr; // owned by m_ssl
    bool m_handshakeDone = false;
    QByteArray m_pendingWrite; // written before the handshake finished
};
