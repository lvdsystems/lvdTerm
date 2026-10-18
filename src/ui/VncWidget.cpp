#include "VncWidget.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "transports/vnc/VncClient.h"

namespace
{
// X11 keysyms for keys that have no printable character.
quint32 specialKeysym(int key)
{
    switch (key) {
    case Qt::Key_Backspace: return 0xff08;
    case Qt::Key_Tab: return 0xff09;
    case Qt::Key_Backtab: return 0xff09;
    case Qt::Key_Return: return 0xff0d;
    case Qt::Key_Enter: return 0xff8d;
    case Qt::Key_Escape: return 0xff1b;
    case Qt::Key_Insert: return 0xff63;
    case Qt::Key_Delete: return 0xffff;
    case Qt::Key_Home: return 0xff50;
    case Qt::Key_Left: return 0xff51;
    case Qt::Key_Up: return 0xff52;
    case Qt::Key_Right: return 0xff53;
    case Qt::Key_Down: return 0xff54;
    case Qt::Key_PageUp: return 0xff55;
    case Qt::Key_PageDown: return 0xff56;
    case Qt::Key_End: return 0xff57;
    case Qt::Key_Shift: return 0xffe1;
    case Qt::Key_Control: return 0xffe3;
    case Qt::Key_Alt: return 0xffe9;
    case Qt::Key_AltGr: return 0xfe03;
    case Qt::Key_Meta: return 0xffeb; // the Windows/Super key
    case Qt::Key_CapsLock: return 0xffe5;
    case Qt::Key_NumLock: return 0xff7f;
    case Qt::Key_ScrollLock: return 0xff14;
    case Qt::Key_Pause: return 0xff13;
    case Qt::Key_Print: return 0xff61;
    case Qt::Key_Menu: return 0xff67;
    default: break;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return 0xffbe + static_cast<quint32>(key - Qt::Key_F1);
    return 0;
}
} // namespace

VncWidget::VncWidget(QWidget *parent)
    : QWidget(parent)
{
    // Whatever is copied locally is offered to the remote machine right away
    // (not only when this pane gets focus), so Ctrl+V there pastes it.
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this] {
        if (!m_client || !m_client->isReady())
            return;
        const QString text = QGuiApplication::clipboard()->text();
        if (!text.isEmpty() && text != m_lastSentClipboard) {
            m_lastSentClipboard = text;
            m_client->sendClipboardText(text);
        }
    });
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);
}

void VncWidget::setClient(VncClient *client)
{
    if (m_client)
        m_client->disconnect(this);
    m_client = client;
    m_pressedKeys.clear();
    if (m_client) {
        connect(m_client, &VncClient::framebufferChanged, this, [this](const QRect &dirty) {
            // Repaint only what changed (with a margin for the scaling filter).
            const QSize fb = m_client ? m_client->framebuffer().size() : QSize();
            if (fb.isEmpty()) {
                update();
                return;
            }
            const QRectF target = targetRect();
            const qreal sx = target.width() / fb.width();
            const qreal sy = target.height() / fb.height();
            const QRectF area(target.x() + dirty.x() * sx, target.y() + dirty.y() * sy, dirty.width() * sx, dirty.height() * sy);
            update(area.toAlignedRect().adjusted(-2, -2, 2, 2));
        });
        connect(m_client, &VncClient::framebufferResized, this, [this](const QSize &) { update(); });
        connect(m_client, &VncClient::clipboardTextReceived, this, [this](const QString &text) {
            m_lastSentClipboard = text; // don't echo it straight back
            QGuiApplication::clipboard()->setText(text);
        });
    }
    update();
}

QRectF VncWidget::targetRect() const
{
    if (!m_client || m_client->framebuffer().isNull())
        return rect();
    const QSizeF fb = m_client->framebuffer().size();
    const qreal scale = qMin(width() / fb.width(), height() / fb.height());
    const QSizeF size(fb.width() * scale, fb.height() * scale);
    return QRectF(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), size);
}

QPoint VncWidget::toRemote(const QPointF &pos) const
{
    const QRectF target = targetRect();
    if (!m_client || m_client->framebuffer().isNull() || target.isEmpty())
        return QPoint();
    const QSize fb = m_client->framebuffer().size();
    const int x = static_cast<int>((pos.x() - target.x()) * fb.width() / target.width());
    const int y = static_cast<int>((pos.y() - target.y()) * fb.height() / target.height());
    return QPoint(qBound(0, x, fb.width() - 1), qBound(0, y, fb.height() - 1));
}

void VncWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!m_client || m_client->framebuffer().isNull())
        return;
    // Smooth filtering only when actually scaled: at 1:1 it would just cost time.
    const QRectF target = targetRect();
    const QImage &fb = m_client->framebuffer();
    p.setRenderHint(QPainter::SmoothPixmapTransform, !qFuzzyCompare(target.width(), static_cast<qreal>(fb.width())));
    p.drawImage(target, fb);
}

void VncWidget::sendPointer(const QPointF &pos, Qt::MouseButtons buttons, int wheelBits)
{
    if (!m_client)
        return;
    int mask = wheelBits;
    if (buttons & Qt::LeftButton)
        mask |= 1;
    if (buttons & Qt::MiddleButton)
        mask |= 2;
    if (buttons & Qt::RightButton)
        mask |= 4;
    const QPoint p = toRemote(pos);
    m_client->sendPointer(p.x(), p.y(), mask);
}

void VncWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    if (event->button() == Qt::RightButton && (event->modifiers() & Qt::ShiftModifier)) {
        emit contextMenuRequested(event->globalPosition().toPoint());
        return;
    }
    sendPointer(event->position(), event->buttons());
}

void VncWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton && (event->modifiers() & Qt::ShiftModifier))
        return;
    sendPointer(event->position(), event->buttons());
}

void VncWidget::mouseMoveEvent(QMouseEvent *event)
{
    sendPointer(event->position(), event->buttons());
}

void VncWidget::wheelEvent(QWheelEvent *event)
{
    // The wheel is buttons 4 (up) and 5 (down): a press and release each.
    const int dy = event->angleDelta().y();
    if (dy == 0 || !m_client)
        return;
    const int bit = dy > 0 ? 8 : 16;
    const int steps = qMax(1, qAbs(dy) / 120);
    for (int i = 0; i < steps; ++i) {
        sendPointer(event->position(), event->buttons(), bit);
        sendPointer(event->position(), event->buttons(), 0);
    }
}

quint32 VncWidget::keysymFor(const QKeyEvent *event)
{
    if (const quint32 special = specialKeysym(event->key()))
        return special;

    // What the key actually produced (shift and layout included): Latin-1
    // keysyms equal their code point, anything else is 0x01000000 + it.
    const QString text = event->text();
    if (!text.isEmpty()) {
        const char32_t cp = text.toUcs4().value(0);
        if (cp >= 0x20 && cp != 0x7f)
            return cp <= 0xff ? cp : 0x01000000 + cp;
    }

    // Ctrl/Alt combinations produce no text: fall back to the bare key,
    // lower-case letters being the unshifted keysyms.
    const int key = event->key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return static_cast<quint32>(key - Qt::Key_A + 'a');
    if (key >= 0x20 && key <= 0x7e)
        return static_cast<quint32>(key);
    return 0;
}

void VncWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_client) {
        QWidget::keyPressEvent(event);
        return;
    }
    const quint32 keysym = keysymFor(event);
    if (!keysym)
        return;
    // An auto-repeat arrives as more presses, which is what a remote key
    // repeat looks like too.
    m_pressedKeys.insert(event->key(), keysym);
    m_client->sendKey(keysym, true);
}

void VncWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (!m_client || event->isAutoRepeat())
        return;
    // Released with the keysym it was pressed with (a release carries no text).
    const auto it = m_pressedKeys.find(event->key());
    if (it == m_pressedKeys.end())
        return;
    m_client->sendKey(it.value(), false);
    m_pressedKeys.erase(it);
}

void VncWidget::releaseAllKeys()
{
    if (m_client) {
        for (const quint32 keysym : std::as_const(m_pressedKeys))
            m_client->sendKey(keysym, false);
    }
    m_pressedKeys.clear();
}

void VncWidget::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    // Local clipboard -> remote, when it changed since we last looked.
    if (m_client) {
        const QString text = QGuiApplication::clipboard()->text();
        if (!text.isEmpty() && text != m_lastSentClipboard) {
            m_lastSentClipboard = text;
            m_client->sendClipboardText(text);
        }
    }
}

void VncWidget::focusOutEvent(QFocusEvent *event)
{
    QWidget::focusOutEvent(event);
    releaseAllKeys(); // otherwise e.g. Alt stays "held" on the remote side after Alt+Tab
}

bool VncWidget::event(QEvent *event)
{
    // Without this the window's own shortcuts (Ctrl+W, Ctrl+T, ...) would
    // be taken before the key ever reached keyPressEvent().
    if (event->type() == QEvent::ShortcutOverride && m_client) {
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

bool VncWidget::focusNextPrevChild(bool)
{
    return false;
}
