#pragma once

#include <QHash>
#include <QPointer>
#include <QWidget>

class RdpClient;

// Shows an RdpClient's remote desktop scaled to fit (aspect ratio kept) and
// forwards mouse, wheel and keyboard to it. The pane's own context menu is
// on Shift+right-click, since a plain right-click belongs to the remote
// machine.
class RdpWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RdpWidget(QWidget *parent = nullptr);

    // Bound to a new client after each reconnect (the old one is dropped).
    void setClient(RdpClient *client);

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
    void focusOutEvent(QFocusEvent *event) override;
    bool focusNextPrevChild(bool next) override; // Tab belongs to the remote machine
    bool event(QEvent *event) override;          // shortcut override: every key goes to the remote machine

private:
    QRectF targetRect() const;
    QPoint toRemote(const QPointF &pos) const;
    void releaseAllKeys();

    struct HeldKey
    {
        quint8 scancode;
        bool extended;
    };

    QPointer<RdpClient> m_client;
    QString m_fromRemote; // the last clipboard text that came from the remote side
    QHash<int, HeldKey> m_heldKeys; // Qt key code -> scancode sent on press, so release matches
};
