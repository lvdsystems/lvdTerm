#pragma once

#include <cstring>
#include <functional>
#include <memory>

#include <QByteArray>
#include <QtEndian>
#include <QImage>
#include <QObject>
#include <QList>
#include <QMap>
#include <QRect>

class QTcpSocket;
class VncTls;

// lvdterm's own VNC (RFB 3.3/3.7/3.8) client: the protocol only, no UI.
// Event-driven on the GUI thread over a QTcpSocket - there is no
// blocking work worth a thread (VncWidget paints straight from
// framebuffer()).
//
// Supported: security None, VNC Authentication (DES challenge), TLS (type
// 18, anonymous) and VeNCrypt (type 19) with its TLS and X509 sub-types
// (None, VNC password or username/password inside TLS - see VncTls);
// encodings Tight (zlib and JPEG), ZRLE, Hextile, CopyRect and Raw plus the
// DesktopSize and LastRect pseudo-encodings; pointer, keyboard and clipboard
// text both ways. Not (yet) supported: VeNCrypt's unencrypted Plain
// sub-type (it would send the password in the clear) and the cursor
// pseudo-encodings (the server draws the cursor into the framebuffer
// instead). The compressed encodings matter a lot on a busy desktop: a
// window drag over a photograph is ~20x less data as Tight/JPEG than as Hextile.
//
// Encrypted security types are preferred when the server offers them. If
// the TLS handshake itself fails and the server also offered a plain type,
// the client reconnects once using that instead.
//
// The wire pixel format is fixed to 32-bit true colour laid out like
// QImage::Format_RGB32, so decoded pixels are copied in without conversion.
class VncClient : public QObject
{
    Q_OBJECT

public:
    struct InflateState;

    explicit VncClient(QObject *parent = nullptr);
    ~VncClient() override;

    void connectToHost(const QString &host, quint16 port, const QString &password);
    void close();
    bool isReady() const { return m_stage == Stage::Running; }

    // Asked for the password when the server wants one and none was given.
    // Returns false if the user cancelled.
    void setPasswordProvider(std::function<bool(QString &)> provider) { m_passwordProvider = std::move(provider); }
    // Asked for a username and password (VeNCrypt's *Plain sub-types).
    void setCredentialsProvider(std::function<bool(QString &user, QString &password)> provider) { m_credentialsProvider = std::move(provider); }
    // VeNCrypt X509 sub-types: asked whether to trust the server's
    // certificate (SHA-256 fingerprint, subject, issuer). False ends the
    // connection. Without a checker, any certificate is refused.
    void setCertificateChecker(std::function<bool(const QString &fingerprint, const QString &subject, const QString &issuer)> checker)
    {
        m_certificateChecker = std::move(checker);
    }
    // Whether the current session is encrypted (for display).
    bool isEncrypted() const { return m_tls != nullptr; }

    // Encodings to offer, most preferred first (default: Hextile, CopyRect,
    // Raw plus the DesktopSize pseudo-encoding). For diagnosing a server.
    void setEncodings(const QList<qint32> &encodings) { m_encodings = encodings; }

    // Counters, for diagnosing a slow or misbehaving server.
    struct Stats
    {
        quint64 bytesIn = 0;
        quint64 updates = 0;
        qint64 decodeNanos = 0; // time spent decoding what arrived
        QMap<qint32, quint64> rectsByEncoding;
        QMap<qint32, quint64> pixelsByEncoding;
    };
    const Stats &stats() const { return m_stats; }

    const QImage &framebuffer() const { return m_fb; }
    QString desktopName() const { return m_desktopName; }

    void sendPointer(int x, int y, int buttonMask);
    void sendKey(quint32 keysym, bool down);
    void sendClipboardText(const QString &text);

    // DES-encrypts a 16-byte VNC auth challenge with `password` (exposed for testing).
    static QByteArray encryptChallenge(const QByteArray &challenge, const QString &password);

signals:
    void ready(); // handshake done, framebuffer exists
    void framebufferChanged(const QRect &dirty);
    void framebufferResized(const QSize &size);
    void clipboardTextReceived(const QString &text);
    void bell();
    void failed(const QString &message); // connect/protocol/auth error
    void closed();                       // connection ended (after failed(), too)

private:
    enum class Stage
    {
        Idle,
        Version,
        SecurityTypes,   // 3.7/3.8: list; 3.3: a single type
        VeNCryptVersion,  // server's VeNCrypt version (2 bytes)
        VeNCryptStatus,   // 1 byte: 0 = our version reply was accepted
        VeNCryptSubtypes, // count + that many u32 sub-types
        VeNCryptAck,      // 1 byte: 1 = our sub-type choice was accepted
        TlsHandshake,     // waiting for VncTls; then m_afterTls
        AuthChallenge,
        SecurityResult,
        SecurityReason,  // reason string after a failed result / empty list
        ServerInit,
        Running,
    };

    void onReadyRead();
    void onSocketClosed();
    bool step(); // consumes what it can of m_buf for the current stage; false = needs more data
    bool stepVersion();
    bool stepSecurityTypes();
    bool stepVeNCryptVersion();
    bool stepVeNCryptStatus();
    bool stepVeNCryptSubtypes();
    bool stepVeNCryptAck();
    bool stepAuthChallenge();
    bool stepSecurityResult();
    bool stepSecurityReason();
    bool stepServerInit();
    bool stepRunning();
    bool stepRect();
    bool stepHextileTile();
    bool stepTight();
    bool stepZrle();
    bool setPixel(int x, int y, QRgb color);

    void beginSecurity(quint8 type);
    // What comes after the TLS handshake (VeNCrypt sub-type / type 18).
    enum class AfterTls
    {
        SecurityTypes,  // type 18: the server lists the inner types again
        SecurityResult, // *None
        AuthChallenge,  // *Vnc
        Plain,          // *Plain: send username and password
    };
    void startTls(bool x509, AfterTls after);
    void feedTls(const QByteArray &cipher);
    void flushTls();
    void onTlsHandshakeDone();
    void failTls(const QString &message); // may fall back to a plain security type
    void sendPlainCredentials();
    void send(const QByteArray &data); // through TLS once it is on
    void sendClientInit();
    void sendPixelFormatAndEncodings();
    void requestUpdate(bool incremental);
    void startConnection();
    bool retryOlderProtocol();
    void fail(const QString &message);
    void finishUpdate();

    void allocFramebuffer(const QSize &size);
    void fillRect(int x, int y, int w, int h, QRgb color);
    void addDirty(const QRect &r) { m_dirty = m_dirty.united(r); }

    // Big-endian reads from the front of m_buf (+ off).
    quint8 u8(int off) const { return static_cast<quint8>(m_buf[m_pos + off]); }
    int avail() const { return static_cast<int>(m_buf.size()) - m_pos; }
    void consume(int n) { m_pos += n; }
    quint16 u16(int off) const { return static_cast<quint16>((u8(off) << 8) | u8(off + 1)); }
    quint32 u32(int off) const { return (static_cast<quint32>(u16(off)) << 16) | u16(off + 2); }
    // Wire pixel: B, G, R, pad - a little-endian 0x00RRGGBB.
    QRgb pixelAt(int off) const
    {
        quint32 v;
        memcpy(&v, m_buf.constData() + m_pos + off, 4);
        return 0xff000000u | qFromLittleEndian(v);
    }

    QTcpSocket *m_socket = nullptr;
    QByteArray m_buf;
    int m_pos = 0; // start of unconsumed data in m_buf
    Stage m_stage = Stage::Idle;
    std::function<bool(QString &)> m_passwordProvider;
    std::function<bool(QString &, QString &)> m_credentialsProvider;
    std::function<bool(const QString &, const QString &, const QString &)> m_certificateChecker;

    std::unique_ptr<VncTls> m_tls;
    bool m_tlsX509 = false;
    quint32 m_veNCryptSubtype = 0;
    // onTlsHandshakeDone() is asking the user (certificate, credentials):
    // data arriving meanwhile must not run it a second time.
    bool m_inTlsDone = false;
    AfterTls m_afterTls = AfterTls::SecurityTypes;
    bool m_allowTls = true;     // cleared to retry with a plain type after a failed handshake
    bool m_plainOffered = false; // the server also offered None or VNC password
    QString m_username;

    QString m_password;
    int m_minor = 3;            // negotiated protocol 3.x
    int m_maxMinor = 8;         // lowered when a server rejects our newer reply
    QString m_host;
    QList<qint32> m_encodings;
    quint16 m_port = 5900;
    quint8 m_securityType = 0;
    bool m_failedAlready = false;

    Stats m_stats;
    QImage m_fb;   // what is displayed: only changed at the end of an update
    QImage m_work; // where updates are decoded into
    QString m_desktopName;
    QRect m_dirty;
    std::unique_ptr<InflateState> m_tightStreams[4];
    std::unique_ptr<InflateState> m_zrleStream;
    bool m_lastRectMode = false; // the update announced 0xFFFF rects: it ends with a LastRect
    bool m_resized = false; // a DesktopSize rect arrived in the current update

    // Framebuffer update in progress.
    int m_rectsLeft = 0;
    bool m_inRect = false;
    QRect m_rect;
    qint32 m_encoding = 0;
    int m_tileX = 0;
    int m_tileY = 0;
    QRgb m_hexBg = qRgb(0, 0, 0);
    QRgb m_hexFg = qRgb(255, 255, 255);
};
