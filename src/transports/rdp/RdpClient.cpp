#include "RdpClient.h"

#include <atomic>

#include <QMetaObject>
#include <QThread>

#include <freerdp/codec/color.h>
#include <freerdp/error.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <winpr/synch.h>
#include <winpr/wlog.h>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace
{
struct InputEvent
{
    enum class Kind
    {
        Mouse,
        Scancode,
        Unicode,
    };
    Kind kind = Kind::Mouse;
    quint16 flags = 0;
    quint16 x = 0;
    quint16 y = 0;
    quint16 code = 0;
};
} // namespace

// The session: owns the FreeRDP instance and runs its event loop.
class RdpThread : public QThread
{
public:
    RdpThread(RdpClient *client, const RdpConnectionSettings &settings)
        : m_client(client)
        , m_settings(settings)
        , m_wake(CreateEventA(nullptr, TRUE, FALSE, nullptr))
    {
    }

    ~RdpThread() override
    {
        requestStop();
        wait();
        if (m_wake)
            CloseHandle(m_wake);
    }

    void requestStop()
    {
        m_stop = true;
        if (rdpContext *context = m_context.load())
            freerdp_abort_connect_context(context);
        if (m_wake)
            SetEvent(m_wake);
    }

    void push(const InputEvent &event)
    {
        {
            std::lock_guard<std::mutex> lock(m_inputMutex);
            if (m_input.size() > 4096)
                return; // the session is not draining it; drop rather than grow without bound
            m_input.push_back(event);
        }
        if (m_wake)
            SetEvent(m_wake);
    }

    // FreeRDP callbacks (see run()).
    static BOOL postConnect(freerdp *instance);
    static void postDisconnect(freerdp *instance);
    static BOOL endPaint(rdpContext *context);
    static BOOL desktopResize(rdpContext *context);
    static BOOL authenticate(freerdp *instance, char **username, char **password, char **domain, rdp_auth_reason reason);

    void copyRect(int x, int y, int w, int h);
    void reallocImage(int w, int h);

protected:
    void run() override;

private:
    struct Context
    {
        rdpContext base; // must be first: FreeRDP allocates ContextSize bytes and treats it as a rdpContext
        RdpThread *thread;
    };

    static RdpThread *threadOf(rdpContext *context) { return reinterpret_cast<Context *>(context)->thread; }
    static RdpThread *threadOf(freerdp *instance) { return threadOf(instance->context); }

    void applySettings(rdpSettings *settings);
    void drainInput(rdpContext *context);
    QString errorText(rdpContext *context, freerdp *instance) const;

    RdpClient *m_client;
    RdpConnectionSettings m_settings;
    HANDLE m_wake;
    std::atomic<bool> m_stop{false};
    std::atomic<rdpContext *> m_context{nullptr};

    std::mutex m_inputMutex;
    std::deque<InputEvent> m_input;

    friend class RdpClient;
};

namespace
{
char *dup(const QString &s)
{
    const QByteArray utf8 = s.toUtf8();
    char *copy = static_cast<char *>(malloc(static_cast<size_t>(utf8.size()) + 1));
    memcpy(copy, utf8.constData(), static_cast<size_t>(utf8.size()) + 1);
    return copy;
}
} // namespace

void RdpThread::applySettings(rdpSettings *settings)
{
    freerdp_settings_set_string(settings, FreeRDP_ServerHostname, m_settings.host.toUtf8().constData());
    freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, m_settings.port);
    if (!m_settings.username.isEmpty())
        freerdp_settings_set_string(settings, FreeRDP_Username, m_settings.username.toUtf8().constData());
    if (!m_settings.domain.isEmpty())
        freerdp_settings_set_string(settings, FreeRDP_Domain, m_settings.domain.toUtf8().constData());
    if (!m_settings.password.isEmpty())
        freerdp_settings_set_string(settings, FreeRDP_Password, m_settings.password.toUtf8().constData());

    freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, static_cast<UINT32>(m_settings.desktopWidth));
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, static_cast<UINT32>(m_settings.desktopHeight));
    freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32);
    freerdp_settings_set_uint32(settings, FreeRDP_KeyboardLayout, 0x409);
    freerdp_settings_set_bool(settings, FreeRDP_SoftwareGdi, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_UnicodeInput, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_AutoReconnectionEnabled, FALSE); // lvdterm reconnects by itself
    // No graphics-pipeline channel in this build: the classic bitmap path.
    freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, FALSE);
    // Server certificates are not checked yet (no trust store/prompt).
    freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, TRUE);
}

void RdpThread::run()
{
    WLog_SetLogLevel(WLog_GetRoot(), WLOG_ERROR);

#ifdef _WIN32
    // FreeRDP uses Winsock but leaves starting it to the application (it is
    // reference counted, so this is harmless if something else did already).
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    freerdp *instance = freerdp_new();
    if (!instance) {
        emit m_client->failed(QStringLiteral("Could not initialise the RDP library"));
        emit m_client->closed();
        return;
    }
    instance->ContextSize = sizeof(Context);
    instance->PostConnect = &RdpThread::postConnect;
    instance->PostDisconnect = &RdpThread::postDisconnect;
    instance->AuthenticateEx = &RdpThread::authenticate;

    if (!freerdp_context_new(instance)) {
        freerdp_free(instance);
        emit m_client->failed(QStringLiteral("Could not create the RDP session"));
        emit m_client->closed();
        return;
    }
    rdpContext *context = instance->context;
    reinterpret_cast<Context *>(context)->thread = this;
    m_context = context;
    applySettings(context->settings);

    QString failure;
    bool connected = false;

    if (m_stop) {
        // closed before it started
    } else if (!freerdp_connect(instance)) {
        if (!m_stop)
            failure = errorText(context, instance);
    } else {
        connected = true;
        HANDLE handles[64];
        while (!m_stop && !freerdp_shall_disconnect_context(context)) {
            DWORD count = freerdp_get_event_handles(context, handles, 63);
            if (count == 0) {
                failure = QStringLiteral("The RDP connection failed");
                break;
            }
            handles[count++] = m_wake;
            const DWORD status = WaitForMultipleObjects(count, handles, FALSE, 200);
            if (status == WAIT_FAILED)
                break;
            if (!freerdp_check_event_handles(context)) {
                failure = errorText(context, instance);
                break;
            }
            ResetEvent(m_wake);
            drainInput(context);
        }
        if (failure.isEmpty() && !m_stop) {
            // Ended by the server: a normal logoff is not an error.
            const UINT32 info = freerdp_error_info(instance);
            if (info != ERRINFO_SUCCESS && info != ERRINFO_LOGOFF_BY_USER && info != ERRINFO_RPC_INITIATED_LOGOFF
                && info != ERRINFO_RPC_INITIATED_DISCONNECT)
                failure = errorText(context, instance);
        }
        freerdp_disconnect(instance);
    }

    m_context = nullptr;
    m_client->m_ready = false;
    freerdp_context_free(instance);
    freerdp_free(instance);

    if (!failure.isEmpty() && !m_stop)
        emit m_client->failed(failure);
    (void)connected;
    emit m_client->closed();
#ifdef _WIN32
    WSACleanup();
#endif
}

QString RdpThread::errorText(rdpContext *context, freerdp *instance) const
{
    const UINT32 error = freerdp_get_last_error(context);
    if (error != FREERDP_ERROR_SUCCESS) {
        const char *text = freerdp_get_last_error_string(error);
        const char *name = freerdp_get_last_error_name(error);
        return QString::fromUtf8(text && *text ? text : name);
    }
    const UINT32 info = freerdp_error_info(instance);
    if (info != ERRINFO_SUCCESS)
        return QStringLiteral("The server ended the session (error 0x%1)").arg(info, 0, 16);
    return QStringLiteral("The RDP connection failed");
}

void RdpThread::drainInput(rdpContext *context)
{
    std::deque<InputEvent> events;
    {
        std::lock_guard<std::mutex> lock(m_inputMutex);
        events.swap(m_input);
    }
    rdpInput *input = context->input;
    for (const InputEvent &e : events) {
        switch (e.kind) {
        case InputEvent::Kind::Mouse:
            freerdp_input_send_mouse_event(input, e.flags, e.x, e.y);
            break;
        case InputEvent::Kind::Scancode:
            freerdp_input_send_keyboard_event(input, e.flags, static_cast<UINT8>(e.code));
            break;
        case InputEvent::Kind::Unicode:
            freerdp_input_send_unicode_keyboard_event(input, e.flags, e.code);
            break;
        }
    }
}

BOOL RdpThread::postConnect(freerdp *instance)
{
    if (!gdi_init(instance, PIXEL_FORMAT_BGRX32))
        return FALSE;

    rdpContext *context = instance->context;
    context->update->EndPaint = &RdpThread::endPaint;
    context->update->DesktopResize = &RdpThread::desktopResize;

    RdpThread *self = threadOf(instance);
    self->reallocImage(static_cast<int>(context->gdi->width), static_cast<int>(context->gdi->height));
    self->m_client->m_ready = true;
    emit self->m_client->ready();
    return TRUE;
}

void RdpThread::postDisconnect(freerdp *instance)
{
    gdi_free(instance);
}

void RdpThread::reallocImage(int w, int h)
{
    std::lock_guard<std::mutex> lock(m_client->m_imageMutex);
    m_client->m_image = QImage(w, h, QImage::Format_RGB32);
    m_client->m_image.fill(Qt::black);
}

void RdpThread::copyRect(int x, int y, int w, int h)
{
    rdpGdi *gdi = m_context.load()->gdi;
    if (!gdi || !gdi->primary_buffer)
        return;

    std::lock_guard<std::mutex> lock(m_client->m_imageMutex);
    QImage &image = m_client->m_image;
    if (image.isNull())
        return;
    x = qBound(0, x, image.width());
    y = qBound(0, y, image.height());
    w = qMin(w, image.width() - x);
    h = qMin(h, image.height() - y);
    if (w <= 0 || h <= 0)
        return;
    for (int row = y; row < y + h; ++row) {
        const BYTE *source = gdi->primary_buffer + static_cast<size_t>(row) * gdi->stride + static_cast<size_t>(x) * 4;
        memcpy(image.scanLine(row) + x * 4, source, static_cast<size_t>(w) * 4);
    }
}

BOOL RdpThread::endPaint(rdpContext *context)
{
    rdpGdi *gdi = context->gdi;
    if (!gdi || !gdi->primary || !gdi->primary->hdc || !gdi->primary->hdc->hwnd)
        return TRUE;
    const HGDI_WND hwnd = gdi->primary->hdc->hwnd;
    if (!hwnd->invalid || hwnd->invalid->null)
        return TRUE;

    RdpThread *self = threadOf(context);
    self->copyRect(hwnd->invalid->x, hwnd->invalid->y, hwnd->invalid->w, hwnd->invalid->h);
    emit self->m_client->frameChanged();
    return TRUE;
}

BOOL RdpThread::desktopResize(rdpContext *context)
{
    rdpSettings *settings = context->settings;
    const UINT32 w = freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth);
    const UINT32 h = freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight);
    if (!gdi_resize(context->gdi, w, h))
        return FALSE;
    RdpThread *self = threadOf(context);
    self->reallocImage(static_cast<int>(w), static_cast<int>(h));
    self->copyRect(0, 0, static_cast<int>(w), static_cast<int>(h));
    emit self->m_client->frameChanged();
    return TRUE;
}

BOOL RdpThread::authenticate(freerdp *instance, char **username, char **password, char **domain, rdp_auth_reason)
{
    RdpThread *self = threadOf(instance);
    if (self->m_stop || !self->m_client->m_credentialProvider)
        return FALSE;

    QString user = QString::fromUtf8(*username ? *username : "");
    QString pass = QString::fromUtf8(*password ? *password : "");
    QString dom = QString::fromUtf8(*domain ? *domain : "");

    // The credential dialog belongs to the GUI thread; this one waits.
    bool accepted = false;
    QMetaObject::invokeMethod(
        self->m_client, [&] { accepted = self->m_client->m_credentialProvider(user, pass, dom); }, Qt::BlockingQueuedConnection);
    if (!accepted)
        return FALSE;

    free(*username);
    free(*password);
    free(*domain);
    *username = dup(user);
    *password = dup(pass);
    *domain = dup(dom);
    return TRUE;
}

RdpClient::RdpClient(QObject *parent)
    : QObject(parent)
{
}

RdpClient::~RdpClient()
{
    close();
}

void RdpClient::connectToHost(const RdpConnectionSettings &settings)
{
    close();
    m_ready = false;
    m_thread = std::make_unique<RdpThread>(this, settings);
    m_thread->start();
}

void RdpClient::close()
{
    if (m_thread) {
        m_thread->requestStop();
        m_thread->wait();
        m_thread.reset();
    }
    m_ready = false;
}

QSize RdpClient::desktopSize() const
{
    std::lock_guard<std::mutex> lock(m_imageMutex);
    return m_image.size();
}

void RdpClient::paintTo(QPainter *painter, const QRectF &target) const
{
    std::lock_guard<std::mutex> lock(m_imageMutex);
    if (!m_image.isNull())
        painter->drawImage(target, m_image);
}

void RdpClient::sendMouse(int x, int y, Qt::MouseButtons, Qt::MouseButton changed, bool pressed)
{
    if (!m_thread)
        return;
    InputEvent e;
    e.kind = InputEvent::Kind::Mouse;
    e.x = static_cast<quint16>(qBound(0, x, 65535));
    e.y = static_cast<quint16>(qBound(0, y, 65535));
    if (changed == Qt::NoButton) {
        e.flags = PTR_FLAGS_MOVE;
    } else {
        quint16 button = 0;
        if (changed == Qt::LeftButton)
            button = PTR_FLAGS_BUTTON1;
        else if (changed == Qt::RightButton)
            button = PTR_FLAGS_BUTTON2;
        else if (changed == Qt::MiddleButton)
            button = PTR_FLAGS_BUTTON3;
        else
            return;
        e.flags = static_cast<quint16>(button | (pressed ? PTR_FLAGS_DOWN : 0));
    }
    m_thread->push(e);
}

void RdpClient::sendWheel(int x, int y, int delta)
{
    if (!m_thread || delta == 0)
        return;
    // One notch is 120 (0x78); a negative rotation is the two's complement of
    // its magnitude in the low byte, with the "negative" flag set.
    const int magnitude = qBound(1, qAbs(delta), 255);
    const quint16 low = static_cast<quint16>(delta < 0 ? (0x100 - magnitude) & 0xff : magnitude);
    InputEvent e;
    e.kind = InputEvent::Kind::Mouse;
    e.x = static_cast<quint16>(qBound(0, x, 65535));
    e.y = static_cast<quint16>(qBound(0, y, 65535));
    e.flags = static_cast<quint16>(PTR_FLAGS_WHEEL | (delta < 0 ? PTR_FLAGS_WHEEL_NEGATIVE : 0) | low);
    m_thread->push(e);
}

void RdpClient::sendScancode(quint8 scancode, bool extended, bool down)
{
    if (!m_thread)
        return;
    InputEvent e;
    e.kind = InputEvent::Kind::Scancode;
    e.flags = static_cast<quint16>((extended ? KBD_FLAGS_EXTENDED : 0) | (down ? KBD_FLAGS_DOWN : KBD_FLAGS_RELEASE));
    e.code = scancode;
    m_thread->push(e);
}

void RdpClient::sendUnicode(quint16 codepoint, bool down)
{
    if (!m_thread)
        return;
    InputEvent e;
    e.kind = InputEvent::Kind::Unicode;
    e.flags = static_cast<quint16>(down ? KBD_FLAGS_DOWN : KBD_FLAGS_RELEASE);
    e.code = codepoint;
    m_thread->push(e);
}
