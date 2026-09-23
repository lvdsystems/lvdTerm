#pragma once

#include <QObject>
#include <QPointer>

#include "Vt102Emulation.h"

namespace Konsole { class TerminalDisplay; }
using Konsole::TerminalDisplay; // TerminalDisplay lives in namespace Konsole

class Transport;

// Replaces upstream QTermWidget's Session class (see
// third_party/qtermwidget/VENDORING.md). Where Session owned a Pty and fed
// bytes to/from a locally-spawned process, TerminalSession owns a
// Vt102Emulation and feeds bytes to/from a Transport (SSH/Telnet/Serial/
// loopback) instead. The signal/slot wiring below mirrors
// Session::addView() and the QTermWidget constructor in upstream
// qtermwidget.cpp.
class TerminalSession : public QObject
{
    Q_OBJECT

public:
    explicit TerminalSession(QObject *parent = nullptr);
    ~TerminalSession() override;

    // TerminalSession does not take ownership of either object.
    void setTransport(Transport *transport);
    void attachView(TerminalDisplay *view);

    // See KeyboardProfiles.h. name empty means "use the default profile" -
    // same convention as Emulation::setKeyBindings(), which this wraps.
    void setKeyboardProfile(const QString &name) { m_emulation->setKeyBindings(name); }

    Konsole::Vt102Emulation *emulation() const { return m_emulation; }

signals:
    void titleChanged(int titleId, const QString &newTitle);
    void bellRequest(const QString &message);

private slots:
    void onTransportReadyRead(const QByteArray &data);
    void onEmulationSendData(const char *data, int len);
    void onViewSizeChanged(int height, int width);

private:
    void updateTerminalSize();

    Konsole::Vt102Emulation *m_emulation = nullptr;
    QPointer<Transport> m_transport;
    QPointer<TerminalDisplay> m_view;
};
