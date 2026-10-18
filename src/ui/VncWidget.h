#pragma once

#include <QHash>
#include <QPointer>
#include <QWidget>

class VncClient;

// Shows a VncClient's framebuffer scaled to fit (aspect ratio kept) and
// forwards mouse, wheel, keyboard and clipboard to the remote desktop.
// The pane's own context menu is on Shift+right-click, since a plain
// right-click belongs to the remote machine.
class VncWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VncWidget(QWidget *parent = nullptr);

    // Bound to a new client after each reconnect (the old one is dropped).
    void setClient(VncClient *client);

    QSize minimumSizeHint() const override { return QSize(80, 60); }

signals:
    void contextMenuRequested(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool focusNextPrevChild(bool next) override; // Tab belongs to the remote machine
    bool event(QEvent *event) override;          // shortcut override: every key goes to the remote machine

private:
    QRectF targetRect() const;      // where the framebuffer is drawn
    QPoint toRemote(const QPointF &pos) const;
    void sendPointer(const QPointF &pos, Qt::MouseButtons buttons, int wheelBits = 0);
    static quint32 keysymFor(const QKeyEvent *event);
    void releaseAllKeys();

    QPointer<VncClient> m_client;
    QHash<int, quint32> m_pressedKeys; // Qt key code -> keysym sent on press, so release matches
    QString m_lastSentClipboard;
};
