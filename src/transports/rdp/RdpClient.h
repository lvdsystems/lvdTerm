#pragma once

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

#include <QImage>
#include <QObject>
#include <QPainter>
#include <QString>

#include "RdpConnectionSettings.h"

class RdpThread;

// lvdterm's RDP client: a thin Qt wrapper around the vendored FreeRDP
// library (see third_party/freerdp). FreeRDP's connect/event loop block, so
// the session runs on its own thread; the remote desktop is drawn by
// FreeRDP's software GDI and copied into an image that the widget paints
// from (paintTo()), under a mutex.
//
// Supported: NLA/TLS/RDP security, the legacy bitmap and RemoteFX paths,
// mouse, keyboard (scan codes, plus Unicode for printable text), text
// clipboard in both directions (the "cliprdr" virtual channel) and server-side
// desktop resizes. Not (yet) supported: other virtual channels (audio,
// drive/printer redirection, files in the clipboard) and dynamic resolution -
// this build of FreeRDP has only the clipboard channel - and certificates
// are not verified.
//
// All signals are emitted from the session thread; connections to GUI
// objects are queued automatically.
class RdpClient : public QObject
{
    Q_OBJECT

public:
    explicit RdpClient(QObject *parent = nullptr);
    ~RdpClient() override;

    // Called (on the GUI thread, blocking the session) when the server wants
    // credentials that were not supplied or were rejected. Returns false to
    // abort the connection.
    using CredentialProvider = std::function<bool(QString &username, QString &password, QString &domain)>;
    void setCredentialProvider(CredentialProvider provider) { m_credentialProvider = std::move(provider); }

    void connectToHost(const RdpConnectionSettings &settings);
    void close(); // stops the session and waits for its thread
    bool isReady() const { return m_ready.load(); }

    QSize desktopSize() const;
    // Draws the remote desktop scaled into `target`.
    void paintTo(QPainter *painter, const QRectF &target) const;

    // Input (any thread; queued to the session thread).
    void sendMouse(int x, int y, Qt::MouseButtons buttons, Qt::MouseButton changed, bool pressed);
    void sendWheel(int x, int y, int delta); // delta in 1/8 degree steps as Qt reports (120 = one notch)
    void sendScancode(quint8 scancode, bool extended, bool down);
    void sendUnicode(quint16 codepoint, bool down);

    // Clipboard text (see the class comment). The local text is what the
    // remote side gets when it pastes; remoteClipboardText() fires when the
    // remote side copied something.
    void setLocalClipboardText(const QString &text);

signals:
    void ready();                 // connected, desktop exists
    void frameChanged();
    void failed(const QString &message);
    void closed();                // session over (after failed(), too)
    void remoteClipboardText(const QString &text);

private:
    friend class RdpThread;

    CredentialProvider m_credentialProvider;
    std::atomic<bool> m_ready{false};

    // The local clipboard text, as last set by setLocalClipboardText().
    mutable std::mutex m_clipboardMutex;
    QString m_localClipboard;
    QString localClipboardText() const;

    // The framebuffer copy.
    mutable std::mutex m_imageMutex;
    QImage m_image;

    std::unique_ptr<RdpThread> m_thread;
};
