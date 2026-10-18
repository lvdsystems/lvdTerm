#include "VncClient.h"

#include <cstring>
#include <iterator>

#include <QElapsedTimer>
#include <QTcpSocket>

#include <zlib.h>

#include "JpegDecoder.h"

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/des.h>

namespace
{
constexpr int kMaxDesktopDimension = 16384;
constexpr quint32 kMaxStringLength = 1 << 20;

enum Encoding : qint32
{
    EncRaw = 0,
    EncCopyRect = 1,
    EncHextile = 5,
    EncTight = 7,
    EncZrle = 16,
    EncLastRect = -224,
    EncDesktopSize = -223,
    // Tight's quality (0-9) and compression (0-9) hints to the server.
    EncQualityLevel8 = -24,
    EncCompressLevel6 = -250,
};

// Hextile sub-encoding bits.
constexpr quint8 kHexRaw = 1;
constexpr quint8 kHexBackgroundSpecified = 2;
constexpr quint8 kHexForegroundSpecified = 4;
constexpr quint8 kHexAnySubrects = 8;
constexpr quint8 kHexSubrectsColoured = 16;

// Inflates `in` with a persistent stream. `expected` >= 0: exactly that many
// bytes must come out (Tight); otherwise everything that comes out is kept (ZRLE).
bool inflateChunk(z_stream &zs, const uchar *in, uInt inSize, QByteArray *out, int expected)
{
    zs.next_in = const_cast<Bytef *>(in);
    zs.avail_in = inSize;
    if (expected >= 0) {
        out->resize(expected);
        zs.next_out = reinterpret_cast<Bytef *>(out->data());
        zs.avail_out = static_cast<uInt>(expected);
        const int rc = inflate(&zs, Z_SYNC_FLUSH);
        return (rc == Z_OK || rc == Z_STREAM_END || rc == Z_BUF_ERROR) && zs.avail_out == 0;
    }
    out->clear();
    for (;;) {
        const qsizetype used = out->size();
        out->resize(used + 65536);
        zs.next_out = reinterpret_cast<Bytef *>(out->data()) + used;
        zs.avail_out = 65536;
        const int rc = inflate(&zs, Z_SYNC_FLUSH);
        out->resize(out->size() - static_cast<qsizetype>(zs.avail_out));
        if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR)
            return false;
        if (zs.avail_in == 0 && zs.avail_out != 0)
            return true;
        if (rc == Z_STREAM_END)
            return true;
    }
}

void putU16(QByteArray &b, quint16 v)
{
    b.append(static_cast<char>(v >> 8));
    b.append(static_cast<char>(v & 0xff));
}

void putU32(QByteArray &b, quint32 v)
{
    putU16(b, static_cast<quint16>(v >> 16));
    putU16(b, static_cast<quint16>(v & 0xffff));
}
} // namespace

// A zlib stream that stays open across rectangles (Tight has four, ZRLE one).
struct VncClient::InflateState
{
    z_stream zs{};
    bool ready = false;

    ~InflateState()
    {
        if (ready)
            inflateEnd(&zs);
    }
    bool reset()
    {
        if (!ready) {
            zs = z_stream{};
            if (inflateInit(&zs) != Z_OK)
                return false;
            ready = true;
            return true;
        }
        return inflateReset(&zs) == Z_OK;
    }
};

VncClient::VncClient(QObject *parent)
    : QObject(parent)
{
}

VncClient::~VncClient()
{
    close();
}

void VncClient::connectToHost(const QString &host, quint16 port, const QString &password)
{
    m_host = host;
    m_port = port;
    m_password = password;
    m_maxMinor = 8;
    startConnection();
}

void VncClient::startConnection()
{
    close();
    m_failedAlready = false;
    m_buf.clear();
    m_pos = 0;
    m_stage = Stage::Version;

    m_socket = new QTcpSocket(this);
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_socket, &QTcpSocket::readyRead, this, &VncClient::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &VncClient::onSocketClosed);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (m_stage == Stage::Idle)
            return;
        if (error == QAbstractSocket::RemoteHostClosedError) {
            // A server closing an established session is an ordinary end
            // (onSocketClosed() handles it), not a failure.
            if (m_stage == Stage::Running || retryOlderProtocol())
                return;
        }
        fail(m_socket->errorString());
    });
    m_socket->connectToHost(m_host, m_port);
}

// Some servers announce 3.8 but drop the connection when a client answers
// with it. Hanging up right after our version reply, before sending any
// security types, is that case: reconnect and answer with an older version.
bool VncClient::retryOlderProtocol()
{
    if (m_stage != Stage::SecurityTypes || avail() != 0 || m_minor <= 3)
        return false;
    m_maxMinor = m_minor >= 8 ? 7 : 3;
    startConnection();
    return true;
}

void VncClient::close()
{
    m_stage = Stage::Idle;
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void VncClient::fail(const QString &message)
{
    if (m_failedAlready)
        return;
    m_failedAlready = true;
    close();
    emit failed(message);
    emit closed();
}

void VncClient::onSocketClosed()
{
    if (m_stage == Stage::Idle)
        return;
    if (m_stage == Stage::Running) {
        close();
        emit closed();
    } else if (retryOlderProtocol()) {
        return;
    } else {
        fail(QStringLiteral("Connection closed by the server"));
    }
}

void VncClient::onReadyRead()
{
    const QByteArray chunk = m_socket->readAll();
    m_stats.bytesIn += static_cast<quint64>(chunk.size());
    m_buf.append(chunk);
    QElapsedTimer timer;
    timer.start();
    while (m_stage != Stage::Idle && step()) {
    }
    m_stats.decodeNanos += timer.nsecsElapsed();
    // Drop what was consumed once per read, not once per tile/rect: removing
    // from the front of a large buffer each time is quadratic.
    if (m_pos > 0) {
        m_buf.remove(0, m_pos);
        m_pos = 0;
    }
}

bool VncClient::step()
{
    switch (m_stage) {
    case Stage::Version: return stepVersion();
    case Stage::SecurityTypes: return stepSecurityTypes();
    case Stage::AuthChallenge: return stepAuthChallenge();
    case Stage::SecurityResult: return stepSecurityResult();
    case Stage::SecurityReason: return stepSecurityReason();
    case Stage::ServerInit: return stepServerInit();
    case Stage::Running: return stepRunning();
    case Stage::Idle: break;
    }
    return false;
}

bool VncClient::stepVersion()
{
    if (avail() < 12)
        return false;
    if (!(avail() >= 4 && memcmp(m_buf.constData() + m_pos, "RFB ", 4) == 0)) {
        fail(QStringLiteral("Not a VNC server"));
        return false;
    }
    const int major = m_buf.mid(m_pos + 4, 3).toInt();
    const int minor = m_buf.mid(m_pos + 8, 3).toInt();
    consume(12);

    // Newest version both sides speak; unknown newer servers (and Apple's
    // "3.889") get 3.8.
    if (major > 3 || (major == 3 && minor >= 8))
        m_minor = 8;
    else if (major == 3 && minor == 7)
        m_minor = 7;
    else
        m_minor = 3;
    m_minor = qMin(m_minor, m_maxMinor);

    m_socket->write(QByteArray("RFB 003.00") + QByteArray::number(m_minor) + '\n');
    m_stage = Stage::SecurityTypes;
    return true;
}

void VncClient::beginSecurity(quint8 type)
{
    m_securityType = type;
    if (type == 1) { // None
        if (m_minor >= 8) {
            m_stage = Stage::SecurityResult;
        } else {
            sendClientInit();
        }
    } else if (type == 2) { // VNC Authentication
        m_stage = Stage::AuthChallenge;
    } else {
        fail(QStringLiteral("Unsupported VNC security type %1").arg(type));
    }
}

bool VncClient::stepSecurityTypes()
{
    if (m_minor == 3) {
        if (avail() < 4)
            return false;
        const quint32 type = u32(0);
        consume(4);
        if (type == 0) {
            m_stage = Stage::SecurityReason;
            return true;
        }
        beginSecurity(static_cast<quint8>(type));
        return true;
    }

    if (avail() == 0)
        return false;
    const int count = u8(0);
    if (count == 0) {
        consume(1);
        m_stage = Stage::SecurityReason; // the server explains why
        return true;
    }
    if (avail() < 1 + count)
        return false;

    bool hasNone = false;
    bool hasVncAuth = false;
    for (int i = 0; i < count; ++i) {
        hasNone = hasNone || u8(1 + i) == 1;
        hasVncAuth = hasVncAuth || u8(1 + i) == 2;
    }
    consume(1 + count);

    if (!hasNone && !hasVncAuth) {
        fail(QStringLiteral("The server requires a security type lvdterm does not support (only None and VNC password)"));
        return false;
    }
    const quint8 chosen = hasNone ? 1 : 2;
    m_socket->write(QByteArray(1, static_cast<char>(chosen)));
    beginSecurity(chosen);
    return true;
}

bool VncClient::stepAuthChallenge()
{
    if (avail() < 16)
        return false;
    const QByteArray challenge = m_buf.mid(m_pos, 16);
    consume(16);

    if (m_password.isEmpty() && m_passwordProvider) {
        QString entered;
        if (!m_passwordProvider(entered)) {
            fail(QStringLiteral("Cancelled"));
            return false;
        }
        m_password = entered;
    }

    m_socket->write(encryptChallenge(challenge, m_password));
    m_stage = Stage::SecurityResult;
    return true;
}

bool VncClient::stepSecurityResult()
{
    if (avail() < 4)
        return false;
    const quint32 result = u32(0);
    consume(4);
    if (result == 0) {
        sendClientInit();
        return true;
    }
    if (m_minor >= 8) {
        m_stage = Stage::SecurityReason;
        return true;
    }
    fail(QStringLiteral("Authentication failed (wrong password?)"));
    return false;
}

bool VncClient::stepSecurityReason()
{
    if (avail() < 4)
        return false;
    const quint32 length = u32(0);
    if (length > kMaxStringLength) {
        fail(QStringLiteral("Connection refused"));
        return false;
    }
    if (static_cast<quint32>(avail()) < 4 + length)
        return false;
    const QString reason = QString::fromUtf8(m_buf.mid(m_pos + 4, static_cast<int>(length)));
    consume(4 + static_cast<int>(length));
    fail(reason.isEmpty() ? QStringLiteral("Authentication failed (wrong password?)") : reason);
    return false;
}

void VncClient::sendClientInit()
{
    m_socket->write(QByteArray(1, '\1')); // shared: don't kick other viewers off
    m_stage = Stage::ServerInit;
}

bool VncClient::stepServerInit()
{
    if (avail() < 24)
        return false;
    const quint32 nameLength = u32(20);
    if (nameLength > kMaxStringLength) {
        fail(QStringLiteral("Invalid VNC server name"));
        return false;
    }
    if (static_cast<quint32>(avail()) < 24 + nameLength)
        return false;

    const int width = u16(0);
    const int height = u16(2);
    m_desktopName = QString::fromUtf8(m_buf.mid(m_pos + 24, static_cast<int>(nameLength)));
    consume(24 + static_cast<int>(nameLength));

    if (width <= 0 || height <= 0) {
        fail(QStringLiteral("The server reported an empty desktop"));
        return false;
    }

    allocFramebuffer(QSize(width, height));
    sendPixelFormatAndEncodings();
    m_stage = Stage::Running;
    requestUpdate(false);
    emit ready();
    return true;
}

void VncClient::allocFramebuffer(const QSize &size)
{
    m_work = QImage(size, QImage::Format_RGB32);
    m_work.fill(Qt::black);
    m_fb = m_work.copy();
}

void VncClient::sendPixelFormatAndEncodings()
{
    QByteArray msg;
    msg.append('\0');       // SetPixelFormat
    msg.append(3, '\0');    // padding
    msg.append(char(32));   // bits per pixel
    msg.append(char(24));   // depth
    msg.append('\0');       // little endian
    msg.append('\1');       // true colour
    putU16(msg, 255);       // red max
    putU16(msg, 255);       // green max
    putU16(msg, 255);       // blue max
    msg.append(char(16));   // red shift
    msg.append(char(8));    // green shift
    msg.append('\0');       // blue shift
    msg.append(3, '\0');    // padding

    msg.append(char(2));    // SetEncodings
    msg.append('\0');
    const QList<qint32> encodings = m_encodings.isEmpty()
        ? QList<qint32>{EncTight, EncZrle, EncHextile, EncCopyRect, EncRaw, EncDesktopSize, EncLastRect, EncQualityLevel8, EncCompressLevel6}
        : m_encodings;
    putU16(msg, static_cast<quint16>(encodings.size()));
    for (const qint32 e : encodings)
        putU32(msg, static_cast<quint32>(e));

    m_socket->write(msg);
}

void VncClient::requestUpdate(bool incremental)
{
    if (!m_socket || m_work.isNull())
        return;
    QByteArray msg;
    msg.append(char(3));
    msg.append(incremental ? '\1' : '\0');
    putU16(msg, 0);
    putU16(msg, 0);
    putU16(msg, static_cast<quint16>(m_work.width()));
    putU16(msg, static_cast<quint16>(m_work.height()));
    m_socket->write(msg);
}

bool VncClient::stepRunning()
{
    if (m_inRect || m_rectsLeft > 0)
        return stepRect();

    if (avail() == 0)
        return false;

    switch (u8(0)) {
    case 0: { // FramebufferUpdate
        if (avail() < 4)
            return false;
        m_rectsLeft = u16(2);
        m_lastRectMode = m_rectsLeft == 0xFFFF; // "as many as it takes": ends with a LastRect pseudo-rectangle
        if (m_lastRectMode)
            m_rectsLeft = 1; // never counted down, see stepRect()
        consume(4);
        if (m_rectsLeft == 0)
            finishUpdate();
        return true;
    }
    case 1: { // SetColourMapEntries (unused: true colour)
        if (avail() < 6)
            return false;
        const int total = 6 + 6 * u16(4);
        if (avail() < total)
            return false;
        consume(total);
        return true;
    }
    case 2: // Bell
        consume(1);
        emit bell();
        return true;
    case 3: { // ServerCutText
        if (avail() < 8)
            return false;
        const quint32 length = u32(4);
        if (length > kMaxStringLength) {
            fail(QStringLiteral("Invalid clipboard message"));
            return false;
        }
        if (static_cast<quint32>(avail()) < 8 + length)
            return false;
        const QString text = QString::fromLatin1(m_buf.mid(m_pos + 8, static_cast<int>(length)));
        consume(8 + static_cast<int>(length));
        emit clipboardTextReceived(text);
        return true;
    }
    default:
        fail(QStringLiteral("Unknown VNC message type %1").arg(u8(0)));
        return false;
    }
}

bool VncClient::stepRect()
{
    if (!m_inRect) {
        if (avail() < 12)
            return false;
        m_rect = QRect(u16(0), u16(2), u16(4), u16(6));
        m_encoding = static_cast<qint32>(u32(8));
        consume(12);
        m_inRect = true;
        m_tileX = 0;
        m_tileY = 0;
        m_hexBg = qRgb(0, 0, 0);
        m_hexFg = qRgb(255, 255, 255);

        if (m_encoding != EncDesktopSize && m_encoding != EncLastRect && !QRect(QPoint(0, 0), m_work.size()).contains(m_rect)) {
            fail(QStringLiteral("The server sent an update outside the screen"));
            return false;
        }
    }

    bool rectDone = false;
    switch (m_encoding) {
    case EncRaw: {
        const int w = m_rect.width();
        const int h = m_rect.height();
        const qint64 bytes = static_cast<qint64>(w) * h * 4;
        if (avail() < bytes)
            return false;
        for (int row = 0; row < h; ++row) {
            auto *line = reinterpret_cast<QRgb *>(m_work.scanLine(m_rect.y() + row)) + m_rect.x();
            const int base = row * w * 4;
            for (int col = 0; col < w; ++col)
                line[col] = pixelAt(base + col * 4);
        }
        addDirty(m_rect);
        consume(static_cast<int>(bytes));
        rectDone = true;
        break;
    }
    case EncCopyRect: {
        if (avail() < 4)
            return false;
        const QRect source(u16(0), u16(2), m_rect.width(), m_rect.height());
        consume(4);
        if (!QRect(QPoint(0, 0), m_work.size()).contains(source)) {
            fail(QStringLiteral("The server sent an invalid copy region"));
            return false;
        }
        const QImage copy = m_work.copy(source); // a copy first: source and destination may overlap
        for (int row = 0; row < copy.height(); ++row)
            memcpy(m_work.scanLine(m_rect.y() + row) + m_rect.x() * 4, copy.constScanLine(row), static_cast<size_t>(copy.width()) * 4);
        addDirty(m_rect);
        rectDone = true;
        break;
    }
    case EncHextile: {
        while (m_tileY < m_rect.height()) {
            if (!stepHextileTile())
                return false;
            m_tileX += 16;
            if (m_tileX >= m_rect.width()) {
                m_tileX = 0;
                m_tileY += 16;
            }
        }
        addDirty(m_rect);
        rectDone = true;
        break;
    }
    case EncTight:
        if (!stepTight())
            return false;
        addDirty(m_rect);
        rectDone = true;
        break;
    case EncZrle:
        if (!stepZrle())
            return false;
        rectDone = true;
        break;
    case EncLastRect:
        rectDone = true;
        break;
    case EncDesktopSize: {
        if (m_rect.width() <= 0 || m_rect.height() <= 0 || m_rect.width() > kMaxDesktopDimension || m_rect.height() > kMaxDesktopDimension) {
            fail(QStringLiteral("The server sent an invalid desktop size"));
            return false;
        }
        allocFramebuffer(m_rect.size());
        addDirty(QRect(QPoint(0, 0), m_work.size()));
        emit framebufferResized(m_work.size());
        m_resized = true;
        rectDone = true;
        break;
    }
    default:
        fail(QStringLiteral("The server used an unsupported encoding (%1)").arg(m_encoding));
        return false;
    }

    if (rectDone) {
        m_stats.rectsByEncoding[m_encoding] += 1;
        m_stats.pixelsByEncoding[m_encoding] += static_cast<quint64>(m_rect.width()) * static_cast<quint64>(m_rect.height());
        m_inRect = false;
        if (m_lastRectMode) {
            if (m_encoding == EncLastRect) {
                m_lastRectMode = false;
                m_rectsLeft = 0;
                finishUpdate();
            }
        } else if (--m_rectsLeft == 0) {
            finishUpdate();
        }
    }
    return true;
}

bool VncClient::setPixel(int x, int y, QRgb color)
{
    reinterpret_cast<QRgb *>(m_work.scanLine(y))[x] = color;
    return true;
}

// Tight: a control byte, then one of fill / JPEG / zlib-compressed pixels
// (optionally palette- or gradient-filtered), each in a fixed layout. False
// = incomplete (nothing consumed, retried on more data), or failed.
bool VncClient::stepTight()
{
    if (avail() < 1)
        return false;
    const quint8 control = u8(0);
    int pos = 1;
    const int w = m_rect.width();
    const int h = m_rect.height();
    const int x0 = m_rect.x();
    const int y0 = m_rect.y();

    // Compact length: 1-3 bytes, 7 bits each, low bits first. Returns bytes
    // used, or 0 if they have not all arrived.
    auto compactLength = [&](int at, int *length) -> int {
        if (avail() < at + 1)
            return 0;
        int b = u8(at);
        *length = b & 0x7f;
        if (!(b & 0x80))
            return 1;
        if (avail() < at + 2)
            return 0;
        b = u8(at + 1);
        *length |= (b & 0x7f) << 7;
        if (!(b & 0x80))
            return 2;
        if (avail() < at + 3)
            return 0;
        *length |= u8(at + 2) << 14;
        return 3;
    };

    // The low four bits ask to reset zlib streams 0-3 before this rectangle,
    // whatever kind it is. Repeating it on a retried attempt is harmless: no
    // stream is used in between.
    for (int i = 0; i < 4; ++i) {
        if (control & (1 << i)) {
            if (!m_tightStreams[i])
                m_tightStreams[i] = std::make_unique<InflateState>();
            if (!m_tightStreams[i]->reset()) {
                fail(QStringLiteral("zlib failed"));
                return false;
            }
        }
    }
    const int kind = control >> 4;

    if (kind == 8) { // fill
        if (avail() < pos + 3)
            return false;
        fillRect(x0, y0, w, h, qRgb(u8(pos), u8(pos + 1), u8(pos + 2)));
        consume(pos + 3);
        return true;
    }

    if (kind == 9) { // JPEG
        int length = 0;
        const int n = compactLength(pos, &length);
        if (n == 0 || avail() < pos + n + length)
            return false;
        int jw = 0;
        int jh = 0;
        QByteArray rgb;
        const uchar *data = reinterpret_cast<const uchar *>(m_buf.constData()) + m_pos + pos + n;
        if (!decodeJpegToRgb(data, length, &jw, &jh, &rgb) || jw != w || jh != h) {
            fail(QStringLiteral("The server sent a bad JPEG rectangle"));
            return false;
        }
        const uchar *p = reinterpret_cast<const uchar *>(rgb.constData());
        for (int y = 0; y < h; ++y) {
            auto *line = reinterpret_cast<QRgb *>(m_work.scanLine(y0 + y)) + x0;
            for (int x = 0; x < w; ++x, p += 3)
                line[x] = qRgb(p[0], p[1], p[2]);
        }
        consume(pos + n + length);
        return true;
    }

    if (kind > 9) {
        fail(QStringLiteral("The server used an unsupported Tight sub-encoding"));
        return false;
    }

    // Basic compression.
    const int stream = kind & 3;
    int filter = 0; // 0 copy, 1 palette, 2 gradient
    if (control & 0x40) {
        if (avail() < pos + 1)
            return false;
        filter = u8(pos++);
    }
    int paletteSize = 0;
    QRgb palette[256];
    if (filter == 1) {
        if (avail() < pos + 1)
            return false;
        paletteSize = u8(pos++) + 1;
        if (avail() < pos + paletteSize * 3)
            return false;
        for (int i = 0; i < paletteSize; ++i, pos += 3)
            palette[i] = qRgb(u8(pos), u8(pos + 1), u8(pos + 2));
    } else if (filter > 2) {
        fail(QStringLiteral("The server used an unsupported Tight filter"));
        return false;
    }

    const bool onebit = filter == 1 && paletteSize == 2;
    const int rowBytes = filter == 1 ? (onebit ? (w + 7) / 8 : w) : w * 3;
    const int dataSize = rowBytes * h;

    QByteArray inflated;
    const uchar *pixels = nullptr;
    int consumed = 0;
    if (dataSize < 12) { // too small to be worth compressing: sent as is
        if (avail() < pos + dataSize)
            return false;
        pixels = reinterpret_cast<const uchar *>(m_buf.constData()) + m_pos + pos;
        consumed = pos + dataSize;
    } else {
        int length = 0;
        const int n = compactLength(pos, &length);
        if (n == 0 || avail() < pos + n + length)
            return false;
        if (!m_tightStreams[stream]) {
            m_tightStreams[stream] = std::make_unique<InflateState>();
            if (!m_tightStreams[stream]->reset()) {
                fail(QStringLiteral("zlib failed"));
                return false;
            }
        }
        const uchar *in = reinterpret_cast<const uchar *>(m_buf.constData()) + m_pos + pos + n;
        if (!inflateChunk(m_tightStreams[stream]->zs, in, static_cast<uInt>(length), &inflated, dataSize)) {
            fail(QStringLiteral("The server sent corrupt compressed data"));
            return false;
        }
        pixels = reinterpret_cast<const uchar *>(inflated.constData());
        consumed = pos + n + length;
    }

    if (filter == 1) {
        for (int y = 0; y < h; ++y) {
            auto *line = reinterpret_cast<QRgb *>(m_work.scanLine(y0 + y)) + x0;
            const uchar *row = pixels + y * rowBytes;
            if (onebit) {
                for (int x = 0; x < w; ++x)
                    line[x] = palette[(row[x >> 3] >> (7 - (x & 7))) & 1];
            } else {
                for (int x = 0; x < w; ++x)
                    line[x] = palette[row[x] % paletteSize];
            }
        }
    } else if (filter == 2) {
        // Gradient: each component is predicted from its left, upper and
        // upper-left neighbours (all zero outside the rectangle).
        QByteArray previous(w * 3, '\0');
        QByteArray current(w * 3, '\0');
        for (int y = 0; y < h; ++y) {
            const uchar *row = pixels + y * rowBytes;
            auto *prev = reinterpret_cast<uchar *>(previous.data());
            auto *cur = reinterpret_cast<uchar *>(current.data());
            auto *line = reinterpret_cast<QRgb *>(m_work.scanLine(y0 + y)) + x0;
            for (int x = 0; x < w; ++x) {
                for (int c = 0; c < 3; ++c) {
                    const int left = x ? cur[(x - 1) * 3 + c] : 0;
                    const int above = prev[x * 3 + c];
                    const int aboveLeft = x ? prev[(x - 1) * 3 + c] : 0;
                    const int predicted = qBound(0, left + above - aboveLeft, 255);
                    cur[x * 3 + c] = static_cast<uchar>((row[x * 3 + c] + predicted) & 0xff);
                }
                line[x] = qRgb(cur[x * 3], cur[x * 3 + 1], cur[x * 3 + 2]);
            }
            std::swap(previous, current);
        }
    } else {
        for (int y = 0; y < h; ++y) {
            auto *line = reinterpret_cast<QRgb *>(m_work.scanLine(y0 + y)) + x0;
            const uchar *row = pixels + y * rowBytes;
            for (int x = 0; x < w; ++x)
                line[x] = qRgb(row[x * 3], row[x * 3 + 1], row[x * 3 + 2]);
        }
    }

    consume(consumed);
    return true;
}

// ZRLE: 64x64 tiles, zlib-compressed as one stream for the whole connection.
// A tile is raw, one colour, a packed palette, or run-length coded; colours
// are 3-byte "CPIXELs" (B, G, R for our pixel format).
bool VncClient::stepZrle()
{
    if (avail() < 4)
        return false;
    const quint32 length = u32(0);
    if (length > (64u << 20)) {
        fail(QStringLiteral("The server sent an oversized ZRLE rectangle"));
        return false;
    }
    if (static_cast<quint32>(avail()) < 4 + length)
        return false;

    if (!m_zrleStream) {
        m_zrleStream = std::make_unique<InflateState>();
        if (!m_zrleStream->reset()) {
            fail(QStringLiteral("zlib failed"));
            return false;
        }
    }
    QByteArray inflated;
    const uchar *in = reinterpret_cast<const uchar *>(m_buf.constData()) + m_pos + 4;
    if (!inflateChunk(m_zrleStream->zs, in, static_cast<uInt>(length), &inflated, -1)) {
        fail(QStringLiteral("The server sent corrupt compressed data"));
        return false;
    }
    consume(4 + static_cast<int>(length));

    const uchar *d = reinterpret_cast<const uchar *>(inflated.constData());
    const qsizetype size = inflated.size();
    qsizetype pos = 0;
    bool ok = true;
    auto need = [&](qsizetype n) {
        if (pos + n > size)
            ok = false;
        return ok;
    };
    auto cpixel = [&]() {
        const QRgb c = qRgb(d[pos + 2], d[pos + 1], d[pos]);
        pos += 3;
        return c;
    };

    const int w = m_rect.width();
    const int h = m_rect.height();
    for (int ty = 0; ty < h && ok; ty += 64) {
        for (int tx = 0; tx < w && ok; tx += 64) {
            const int tw = qMin(64, w - tx);
            const int th = qMin(64, h - ty);
            const int x0 = m_rect.x() + tx;
            const int y0 = m_rect.y() + ty;
            if (!need(1))
                break;
            const int sub = d[pos++];
            const bool rle = sub & 128;
            const int paletteSize = sub & 127;

            QRgb palette[128];
            if (paletteSize > 0 && paletteSize != 127) {
                if (!need(paletteSize * 3))
                    break;
                for (int i = 0; i < paletteSize; ++i)
                    palette[i] = cpixel();
            }

            if (!rle) {
                if (paletteSize == 0) { // raw
                    if (!need(tw * th * 3))
                        break;
                    for (int y = 0; y < th; ++y) {
                        auto *line = reinterpret_cast<QRgb *>(m_work.scanLine(y0 + y)) + x0;
                        for (int x = 0; x < tw; ++x)
                            line[x] = cpixel();
                    }
                } else if (paletteSize == 1) { // solid
                    fillRect(x0, y0, tw, th, palette[0]);
                } else { // packed palette
                    const int bits = paletteSize == 2 ? 1 : paletteSize <= 4 ? 2 : 4;
                    const int rowBytes = (tw * bits + 7) / 8;
                    if (!need(rowBytes * th))
                        break;
                    for (int y = 0; y < th; ++y) {
                        auto *line = reinterpret_cast<QRgb *>(m_work.scanLine(y0 + y)) + x0;
                        const uchar *row = d + pos + y * rowBytes;
                        for (int x = 0; x < tw; ++x) {
                            const int shift = 8 - bits * ((x % (8 / bits)) + 1);
                            const int index = (row[x * bits / 8] >> shift) & ((1 << bits) - 1);
                            line[x] = palette[index % paletteSize];
                        }
                    }
                    pos += rowBytes * th;
                }
            } else {
                // Run-length: a colour (or palette index) and a run length,
                // filling the tile in reading order.
                int x = 0;
                int y = 0;
                const int total = tw * th;
                int done = 0;
                while (done < total && ok) {
                    QRgb colour;
                    if (paletteSize == 0) {
                        if (!need(3))
                            break;
                        colour = cpixel();
                    } else {
                        if (!need(1))
                            break;
                        const int index = d[pos++];
                        colour = palette[(index & 127) % paletteSize];
                        if (!(index & 128)) {
                            // A single pixel.
                            reinterpret_cast<QRgb *>(m_work.scanLine(y0 + y))[x0 + x] = colour;
                            if (++x == tw) {
                                x = 0;
                                ++y;
                            }
                            ++done;
                            continue;
                        }
                    }
                    int run = 1;
                    for (;;) {
                        if (!need(1))
                            break;
                        const int b = d[pos++];
                        run += b;
                        if (b != 255)
                            break;
                    }
                    if (!ok || done + run > total) {
                        ok = false;
                        break;
                    }
                    for (int i = 0; i < run; ++i) {
                        reinterpret_cast<QRgb *>(m_work.scanLine(y0 + y))[x0 + x] = colour;
                        if (++x == tw) {
                            x = 0;
                            ++y;
                        }
                    }
                    done += run;
                }
            }
        }
    }
    if (!ok) {
        fail(QStringLiteral("The server sent a corrupt ZRLE rectangle"));
        return false;
    }
    addDirty(m_rect);
    return true;
}

// Decodes one 16x16 (or edge-clipped) tile; false if the data is not all
// here yet (nothing consumed, nothing changed - it is simply retried).
bool VncClient::stepHextileTile()
{
    if (avail() == 0)
        return false;

    const int tw = qMin(16, m_rect.width() - m_tileX);
    const int th = qMin(16, m_rect.height() - m_tileY);
    const int tx = m_rect.x() + m_tileX;
    const int ty = m_rect.y() + m_tileY;
    const quint8 sub = u8(0);
    int pos = 1;

    if (sub & kHexRaw) {
        if (avail() < pos + tw * th * 4)
            return false;
        for (int row = 0; row < th; ++row) {
            auto *line = reinterpret_cast<QRgb *>(m_work.scanLine(ty + row)) + tx;
            for (int col = 0; col < tw; ++col)
                line[col] = pixelAt(pos + (row * tw + col) * 4);
        }
        consume(pos + tw * th * 4);
        return true;
    }

    QRgb bg = m_hexBg;
    QRgb fg = m_hexFg;
    if (sub & kHexBackgroundSpecified) {
        if (avail() < pos + 4)
            return false;
        bg = pixelAt(pos);
        pos += 4;
    }
    if (sub & kHexForegroundSpecified) {
        if (avail() < pos + 4)
            return false;
        fg = pixelAt(pos);
        pos += 4;
    }
    int subrects = 0;
    if (sub & kHexAnySubrects) {
        if (avail() < pos + 1)
            return false;
        subrects = u8(pos);
        pos += 1;
    }
    const bool coloured = sub & kHexSubrectsColoured;
    const int subrectSize = coloured ? 6 : 2;
    if (avail() < pos + subrects * subrectSize)
        return false;

    // Everything is here: commit.
    m_hexBg = bg;
    m_hexFg = fg;
    fillRect(tx, ty, tw, th, bg);
    for (int i = 0; i < subrects; ++i) {
        QRgb colour = fg;
        if (coloured) {
            colour = pixelAt(pos);
            pos += 4;
        }
        const int xy = u8(pos);
        const int wh = u8(pos + 1);
        pos += 2;
        const int sx = xy >> 4;
        const int sy = xy & 15;
        const int sw = (wh >> 4) + 1;
        const int sh = (wh & 15) + 1;
        if (sx + sw > tw || sy + sh > th) {
            fail(QStringLiteral("The server sent an invalid Hextile subrectangle"));
            return false;
        }
        fillRect(tx + sx, ty + sy, sw, sh, colour);
    }
    consume(pos);
    return true;
}

void VncClient::fillRect(int x, int y, int w, int h, QRgb color)
{
    for (int row = 0; row < h; ++row) {
        auto *line = reinterpret_cast<QRgb *>(m_work.scanLine(y + row)) + x;
        for (int col = 0; col < w; ++col)
            line[col] = color;
    }
}

void VncClient::finishUpdate()
{
    ++m_stats.updates;
    const QRect dirty = m_dirty.intersected(QRect(QPoint(0, 0), m_work.size()));
    m_dirty = QRect();
    if (dirty.isValid()) {
        // Decoding happens in the working buffer; the displayed one only
        // ever changes here, whole updates at a time, so a repaint in the
        // middle of a large update never shows it half applied.
        for (int row = dirty.top(); row <= dirty.bottom(); ++row)
            memcpy(m_fb.scanLine(row) + dirty.left() * 4, m_work.constScanLine(row) + dirty.left() * 4, static_cast<size_t>(dirty.width()) * 4);
        emit framebufferChanged(dirty);
    }
    requestUpdate(!m_resized); // a new desktop size needs the whole screen again
    m_resized = false;
}

void VncClient::sendPointer(int x, int y, int buttonMask)
{
    if (m_stage != Stage::Running)
        return;
    QByteArray msg;
    msg.append(char(5));
    msg.append(static_cast<char>(buttonMask));
    putU16(msg, static_cast<quint16>(qBound(0, x, 65535)));
    putU16(msg, static_cast<quint16>(qBound(0, y, 65535)));
    m_socket->write(msg);
}

void VncClient::sendKey(quint32 keysym, bool down)
{
    if (m_stage != Stage::Running)
        return;
    QByteArray msg;
    msg.append(char(4));
    msg.append(down ? '\1' : '\0');
    msg.append(2, '\0');
    putU32(msg, keysym);
    m_socket->write(msg);
}

void VncClient::sendClipboardText(const QString &text)
{
    if (m_stage != Stage::Running)
        return;
    const QByteArray latin1 = text.toLatin1(); // the protocol's clipboard text is Latin-1
    QByteArray msg;
    msg.append(char(6));
    msg.append(3, '\0');
    putU32(msg, static_cast<quint32>(latin1.size()));
    msg.append(latin1);
    m_socket->write(msg);
}

QByteArray VncClient::encryptChallenge(const QByteArray &challenge, const QString &password)
{
    // VNC's DES key is the password's first 8 bytes (zero padded), each byte
    // with its bits in reverse order - a long-standing quirk of the protocol.
    const QByteArray pw = password.toLatin1();
    unsigned char key[8] = {};
    for (int i = 0; i < 8 && i < pw.size(); ++i) {
        unsigned char b = static_cast<unsigned char>(pw[i]);
        unsigned char reversed = 0;
        for (int bit = 0; bit < 8; ++bit)
            reversed = static_cast<unsigned char>((reversed << 1) | ((b >> bit) & 1));
        key[i] = reversed;
    }

    DES_key_schedule schedule;
    DES_set_key_unchecked(reinterpret_cast<const_DES_cblock *>(key), &schedule);

    QByteArray out(16, '\0');
    for (int block = 0; block < 2; ++block) {
        DES_ecb_encrypt(reinterpret_cast<const_DES_cblock *>(const_cast<char *>(challenge.constData()) + block * 8),
                        reinterpret_cast<DES_cblock *>(out.data() + block * 8), &schedule, DES_ENCRYPT);
    }
    return out;
}
