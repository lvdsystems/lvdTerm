#pragma once

#include "core/Transport.h"
#include "VncConnectionSettings.h"

class VncClient;

// Gives a VNC session the same lifecycle every other pane has (state,
// connect/disconnect, errors - so auto-reconnect, the status strip and tab
// icon all work unchanged). There is no byte stream to speak of: the
// pixels travel from client() straight to the pane's VncWidget, and
// write()/resize() have nothing to do.
class VncTransport : public Transport
{
    Q_OBJECT

public:
    explicit VncTransport(VncConnectionSettings settings, QObject *parent = nullptr);

    void connectToHost() override;
    void disconnectFromHost() override;
    void resize(int, int) override {}

    VncClient *client() const { return m_client; }

public slots:
    void write(const QByteArray &) override {}

private:
    VncConnectionSettings m_settings;
    VncClient *m_client = nullptr;
};
