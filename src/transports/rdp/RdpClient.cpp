#include "RdpClient.h"

#include <atomic>

#include <QMetaObject>
#include <QThread>

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/channels/cliprdr.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/codec/color.h>
#include <freerdp/event.h>
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
        ClipboardChanged,
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
    static BOOL loadChannels(freerdp *instance);
    static void onChannelConnected(void *context, const ChannelConnectedEventArgs *e);
    static void onChannelDisconnected(void *context, const ChannelDisconnectedEventArgs *e);
    static UINT clipMonitorReady(CliprdrClientContext *clip, const CLIPRDR_MONITOR_READY *ready);
    static UINT clipServerCapabilities(CliprdrClientContext *clip, const CLIPRDR_CAPABILITIES *caps);
    static UINT clipServerFormatList(CliprdrClientContext *clip, const CLIPRDR_FORMAT_LIST *list);
    static UINT clipServerFormatListResponse(CliprdrClientContext *clip, const CLIPRDR_FORMAT_LIST_RESPONSE *response);
    static UINT clipServerFormatDataRequest(CliprdrClientContext *clip, const CLIPRDR_FORMAT_DATA_REQUEST *request);
    static UINT clipServerFormatDataResponse(CliprdrClientContext *clip, const CLIPRDR_FORMAT_DATA_RESPONSE *response);
    void sendClipboardFormats(); // session thread: tells the server we have text to paste
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

    CliprdrClientContext *m_cliprdr = nullptr; // session thread only
    bool m_clipboardReady = false;             // the server finished the clipboard handshake

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
    instance->LoadChannels = &RdpThread::loadChannels;
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
    PubSub_SubscribeChannelConnected(context->pubSub, &RdpThread::onChannelConnected);
    PubSub_SubscribeChannelDisconnected(context->pubSub, &RdpThread::onChannelDisconnected);
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
            // Ended by the server. Always say why (it is what the pane shows
            // while it reconnects): e.g. a Windows host that allows one session
            // per account drops this one when the same account connects elsewhere.
            const UINT32 info = freerdp_error_info(instance);
            if (info != ERRINFO_SUCCESS) {
                const char *text = freerdp_get_error_info_string(info);
                failure = QStringLiteral("The server ended the session: %1").arg(QString::fromUtf8(text && *text ? text : "unknown reason"));
            } else {
                failure = QStringLiteral("The server closed the connection");
            }
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
        case InputEvent::Kind::ClipboardChanged:
            sendClipboardFormats();
            break;
        }
    }
}

namespace
{
constexpr UINT32 kCfText = 1;
constexpr UINT32 kCfUnicodeText = 13;
} // namespace

// Loads the clipboard channel (the only one in this build), as FreeRDP's own
// client does for "/clipboard".
BOOL RdpThread::loadChannels(freerdp *instance)
{
    static std::atomic<bool> providerRegistered{false};
    if (!providerRegistered.exchange(true)) {
        if (freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0) != CHANNEL_RC_OK)
            return FALSE;
    }

    rdpContext *context = instance->context;
    rdpSettings *settings = context->settings;
    freerdp_settings_set_bool(settings, FreeRDP_RedirectClipboard, TRUE);

    const char *const params[] = {CLIPRDR_SVC_CHANNEL_NAME};
    if (!freerdp_static_channel_collection_find(settings, params[0])) {
        ADDIN_ARGV *args = freerdp_addin_argv_new(1, params);
        if (!args || !freerdp_static_channel_collection_add(settings, args))
            return FALSE;
    }

    // The clipboard channel has the "Ex" entry point.
    PVIRTUALCHANNELENTRY entry =
        freerdp_load_channel_addin_entry(CLIPRDR_SVC_CHANNEL_NAME, nullptr, nullptr, FREERDP_ADDIN_CHANNEL_STATIC | FREERDP_ADDIN_CHANNEL_ENTRYEX);
    if (!entry)
        return FALSE;
    return freerdp_channels_client_load_ex(context->channels, settings, reinterpret_cast<PVIRTUALCHANNELENTRYEX>(entry), settings) == 0;
}

void RdpThread::onChannelConnected(void *context, const ChannelConnectedEventArgs *e)
{
    if (strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME) != 0)
        return;
    auto *clip = static_cast<CliprdrClientContext *>(e->pInterface);
    RdpThread *self = threadOf(static_cast<rdpContext *>(context));
    clip->custom = self;
    clip->MonitorReady = &RdpThread::clipMonitorReady;
    clip->ServerCapabilities = &RdpThread::clipServerCapabilities;
    clip->ServerFormatList = &RdpThread::clipServerFormatList;
    clip->ServerFormatListResponse = &RdpThread::clipServerFormatListResponse;
    clip->ServerFormatDataRequest = &RdpThread::clipServerFormatDataRequest;
    clip->ServerFormatDataResponse = &RdpThread::clipServerFormatDataResponse;
    self->m_cliprdr = clip;
}

void RdpThread::onChannelDisconnected(void *context, const ChannelDisconnectedEventArgs *e)
{
    if (strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME) != 0)
        return;
    RdpThread *self = threadOf(static_cast<rdpContext *>(context));
    self->m_cliprdr = nullptr;
    self->m_clipboardReady = false;
}

UINT RdpThread::clipServerCapabilities(CliprdrClientContext *, const CLIPRDR_CAPABILITIES *)
{
    return CHANNEL_RC_OK;
}

UINT RdpThread::clipMonitorReady(CliprdrClientContext *clip, const CLIPRDR_MONITOR_READY *)
{
    auto *self = static_cast<RdpThread *>(clip->custom);

    CLIPRDR_GENERAL_CAPABILITY_SET general = {};
    general.capabilitySetType = CB_CAPSTYPE_GENERAL;
    general.capabilitySetLength = CB_CAPSTYPE_GENERAL_LEN;
    general.version = CB_CAPS_VERSION_2;
    general.generalFlags = CB_USE_LONG_FORMAT_NAMES;
    CLIPRDR_CAPABILITIES capabilities = {};
    capabilities.cCapabilitiesSets = 1;
    capabilities.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET *>(&general);
    UINT rc = clip->ClientCapabilities(clip, &capabilities);
    if (rc != CHANNEL_RC_OK)
        return rc;

    self->m_clipboardReady = true;
    self->sendClipboardFormats();
    return CHANNEL_RC_OK;
}

void RdpThread::sendClipboardFormats()
{
    if (!m_cliprdr || !m_clipboardReady || m_client->localClipboardText().isEmpty())
        return;

    CLIPRDR_FORMAT formats[1] = {};
    formats[0].formatId = kCfUnicodeText;
    formats[0].formatName = nullptr;
    CLIPRDR_FORMAT_LIST list = {};
    list.common.msgType = CB_FORMAT_LIST;
    list.numFormats = 1;
    list.formats = formats;
    m_cliprdr->ClientFormatList(m_cliprdr, &list);
}

// The remote side copied something: ask for its text if it has any.
UINT RdpThread::clipServerFormatList(CliprdrClientContext *clip, const CLIPRDR_FORMAT_LIST *list)
{
    bool hasUnicode = false;
    bool hasText = false;
    for (UINT32 i = 0; i < list->numFormats; ++i) {
        hasUnicode = hasUnicode || list->formats[i].formatId == kCfUnicodeText;
        hasText = hasText || list->formats[i].formatId == kCfText;
    }

    CLIPRDR_FORMAT_LIST_RESPONSE response = {};
    response.common.msgType = CB_FORMAT_LIST_RESPONSE;
    response.common.msgFlags = CB_RESPONSE_OK;
    UINT rc = clip->ClientFormatListResponse(clip, &response);
    if (rc != CHANNEL_RC_OK || (!hasUnicode && !hasText))
        return rc;

    CLIPRDR_FORMAT_DATA_REQUEST request = {};
    request.common.msgType = CB_FORMAT_DATA_REQUEST;
    request.requestedFormatId = hasUnicode ? kCfUnicodeText : kCfText;
    clip->lastRequestedFormatId = request.requestedFormatId;
    return clip->ClientFormatDataRequest(clip, &request);
}

UINT RdpThread::clipServerFormatListResponse(CliprdrClientContext *, const CLIPRDR_FORMAT_LIST_RESPONSE *)
{
    return CHANNEL_RC_OK;
}

// The remote side pastes: hand it our local clipboard text.
UINT RdpThread::clipServerFormatDataRequest(CliprdrClientContext *clip, const CLIPRDR_FORMAT_DATA_REQUEST *request)
{
    auto *self = static_cast<RdpThread *>(clip->custom);
    QString text = self->m_client->localClipboardText();
    // Windows clipboard text has CRLF line ends.
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\n'), QStringLiteral("\r\n"));

    QByteArray data;
    if (request->requestedFormatId == kCfUnicodeText) {
        data = QByteArray(reinterpret_cast<const char *>(text.utf16()), static_cast<qsizetype>((text.size() + 1) * 2));
    } else if (request->requestedFormatId == kCfText) {
        data = text.toLocal8Bit();
        data.append('\0');
    }

    CLIPRDR_FORMAT_DATA_RESPONSE response = {};
    response.common.msgType = CB_FORMAT_DATA_RESPONSE;
    if (data.isEmpty()) {
        response.common.msgFlags = CB_RESPONSE_FAIL;
    } else {
        response.common.msgFlags = CB_RESPONSE_OK;
        response.common.dataLen = static_cast<UINT32>(data.size());
        response.requestedFormatData = reinterpret_cast<const BYTE *>(data.constData());
    }
    return clip->ClientFormatDataResponse(clip, &response);
}

// The text we asked for (see clipServerFormatList()) arrived.
UINT RdpThread::clipServerFormatDataResponse(CliprdrClientContext *clip, const CLIPRDR_FORMAT_DATA_RESPONSE *response)
{
    auto *self = static_cast<RdpThread *>(clip->custom);
    if (!(response->common.msgFlags & CB_RESPONSE_OK) || !response->requestedFormatData)
        return CHANNEL_RC_OK;

    QString text;
    if (clip->lastRequestedFormatId == kCfUnicodeText) {
        text = QString::fromUtf16(reinterpret_cast<const char16_t *>(response->requestedFormatData), static_cast<qsizetype>(response->common.dataLen / 2));
    } else {
        text = QString::fromLocal8Bit(reinterpret_cast<const char *>(response->requestedFormatData), static_cast<qsizetype>(response->common.dataLen));
    }
    // The text is NUL terminated.
    const qsizetype nul = text.indexOf(QChar(0));
    if (nul >= 0)
        text.truncate(nul);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    emit self->m_client->remoteClipboardText(text);
    return CHANNEL_RC_OK;
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

QString RdpClient::localClipboardText() const
{
    std::lock_guard<std::mutex> lock(m_clipboardMutex);
    return m_localClipboard;
}

void RdpClient::setLocalClipboardText(const QString &text)
{
    {
        std::lock_guard<std::mutex> lock(m_clipboardMutex);
        if (text == m_localClipboard)
            return;
        m_localClipboard = text;
    }
    if (!m_thread)
        return;
    InputEvent e;
    e.kind = InputEvent::Kind::ClipboardChanged;
    m_thread->push(e);
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
