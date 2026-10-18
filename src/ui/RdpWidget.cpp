#include "RdpWidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "transports/rdp/RdpClient.h"

namespace
{
struct Scan
{
    quint8 code = 0;
    bool extended = false;
    bool valid() const { return code != 0; }
};

// US-layout PC/AT (set 1) scan codes - what RDP carries for physical keys.
Scan scanFor(int key)
{
    static const quint8 letters[26] = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                       0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return {letters[key - Qt::Key_A], false};
    if (key >= Qt::Key_1 && key <= Qt::Key_9)
        return {static_cast<quint8>(0x02 + (key - Qt::Key_1)), false};
    if (key >= Qt::Key_F1 && key <= Qt::Key_F10)
        return {static_cast<quint8>(0x3b + (key - Qt::Key_F1)), false};

    switch (key) {
    case Qt::Key_0: return {0x0b, false};
    case Qt::Key_Minus: return {0x0c, false};
    case Qt::Key_Equal: return {0x0d, false};
    case Qt::Key_Backspace: return {0x0e, false};
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return {0x0f, false};
    case Qt::Key_BracketLeft: return {0x1a, false};
    case Qt::Key_BracketRight: return {0x1b, false};
    case Qt::Key_Return: return {0x1c, false};
    case Qt::Key_Enter: return {0x1c, true};
    case Qt::Key_Semicolon: return {0x27, false};
    case Qt::Key_Apostrophe: return {0x28, false};
    case Qt::Key_QuoteLeft: return {0x29, false};
    case Qt::Key_Backslash: return {0x2b, false};
    case Qt::Key_Comma: return {0x33, false};
    case Qt::Key_Period: return {0x34, false};
    case Qt::Key_Slash: return {0x35, false};
    case Qt::Key_Space: return {0x39, false};
    case Qt::Key_Escape: return {0x01, false};
    case Qt::Key_CapsLock: return {0x3a, false};
    case Qt::Key_NumLock: return {0x45, false};
    case Qt::Key_ScrollLock: return {0x46, false};
    case Qt::Key_F11: return {0x57, false};
    case Qt::Key_F12: return {0x58, false};
    case Qt::Key_Shift: return {0x2a, false};
    case Qt::Key_Control: return {0x1d, false};
    case Qt::Key_Alt: return {0x38, false};
    case Qt::Key_AltGr: return {0x38, true};
    case Qt::Key_Meta: return {0x5b, true}; // the Windows key
    case Qt::Key_Menu: return {0x5d, true};
    case Qt::Key_Insert: return {0x52, true};
    case Qt::Key_Delete: return {0x53, true};
    case Qt::Key_Home: return {0x47, true};
    case Qt::Key_End: return {0x4f, true};
    case Qt::Key_PageUp: return {0x49, true};
    case Qt::Key_PageDown: return {0x51, true};
    case Qt::Key_Up: return {0x48, true};
    case Qt::Key_Down: return {0x50, true};
    case Qt::Key_Left: return {0x4b, true};
    case Qt::Key_Right: return {0x4d, true};
    case Qt::Key_Print: return {0x37, true};
    default: return {};
    }
}

bool isModifierKey(int key)
{
    return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_AltGr || key == Qt::Key_Meta;
}
} // namespace

RdpWidget::RdpWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void RdpWidget::setClient(RdpClient *client)
{
    if (m_client)
        m_client->disconnect(this);
    m_client = client;
    m_heldKeys.clear();
    if (m_client) {
        // Emitted from the session thread: queued to this widget.
        connect(m_client, &RdpClient::frameChanged, this, qOverload<>(&QWidget::update), Qt::QueuedConnection);
        connect(m_client, &RdpClient::ready, this, qOverload<>(&QWidget::update), Qt::QueuedConnection);
    }
    update();
}

QRectF RdpWidget::targetRect() const
{
    const QSize desktop = m_client ? m_client->desktopSize() : QSize();
    if (desktop.isEmpty())
        return rect();
    const qreal scale = qMin(static_cast<qreal>(width()) / desktop.width(), static_cast<qreal>(height()) / desktop.height());
    const QSizeF size(desktop.width() * scale, desktop.height() * scale);
    return QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}

QPoint RdpWidget::toRemote(const QPointF &pos) const
{
    const QSize desktop = m_client ? m_client->desktopSize() : QSize();
    const QRectF target = targetRect();
    if (desktop.isEmpty() || target.isEmpty())
        return QPoint();
    const int x = static_cast<int>((pos.x() - target.x()) * desktop.width() / target.width());
    const int y = static_cast<int>((pos.y() - target.y()) * desktop.height() / target.height());
    return QPoint(qBound(0, x, desktop.width() - 1), qBound(0, y, desktop.height() - 1));
}

void RdpWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!m_client)
        return;
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    m_client->paintTo(&p, targetRect());
}

void RdpWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    if (event->button() == Qt::RightButton && (event->modifiers() & Qt::ShiftModifier)) {
        emit contextMenuRequested(event->globalPosition().toPoint());
        return;
    }
    if (!m_client)
        return;
    const QPoint p = toRemote(event->position());
    m_client->sendMouse(p.x(), p.y(), event->buttons(), event->button(), true);
}

void RdpWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_client || (event->button() == Qt::RightButton && (event->modifiers() & Qt::ShiftModifier)))
        return;
    const QPoint p = toRemote(event->position());
    m_client->sendMouse(p.x(), p.y(), event->buttons(), event->button(), false);
}

void RdpWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_client)
        return;
    const QPoint p = toRemote(event->position());
    m_client->sendMouse(p.x(), p.y(), event->buttons(), Qt::NoButton, false);
}

void RdpWidget::wheelEvent(QWheelEvent *event)
{
    if (!m_client)
        return;
    const QPoint p = toRemote(event->position());
    m_client->sendWheel(p.x(), p.y(), event->angleDelta().y());
}

void RdpWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_client) {
        QWidget::keyPressEvent(event);
        return;
    }

    const int key = event->key();
    const QString text = event->text();
    const bool shortcutModifier = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    const char32_t codepoint = text.isEmpty() ? 0 : text.toUcs4().value(0);

    // Plain printable text goes as Unicode, so it comes out right whatever
    // the remote keyboard layout is. Shortcuts and everything else use the
    // physical key.
    if (!shortcutModifier && !isModifierKey(key) && codepoint >= 0x20 && codepoint != 0x7f && codepoint <= 0xffff) {
        m_client->sendUnicode(static_cast<quint16>(codepoint), true);
        m_client->sendUnicode(static_cast<quint16>(codepoint), false);
        return;
    }

    const Scan scan = scanFor(key);
    if (!scan.valid())
        return;
    m_heldKeys.insert(key, {scan.code, scan.extended});
    m_client->sendScancode(scan.code, scan.extended, true);
}

void RdpWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (!m_client || event->isAutoRepeat())
        return;
    const auto it = m_heldKeys.find(event->key());
    if (it == m_heldKeys.end())
        return;
    m_client->sendScancode(it->scancode, it->extended, false);
    m_heldKeys.erase(it);
}

void RdpWidget::releaseAllKeys()
{
    if (m_client) {
        for (const HeldKey &k : std::as_const(m_heldKeys))
            m_client->sendScancode(k.scancode, k.extended, false);
    }
    m_heldKeys.clear();
}

void RdpWidget::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    releaseAllKeys(); // otherwise e.g. Alt stays "held" on the remote side after Alt+Tab
}

bool RdpWidget::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && m_client) {
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

bool RdpWidget::focusNextPrevChild(bool)
{
    return false;
}
