#pragma once

#include <memory>

#include "core/Transport.h"
#include "RdpConnectionSettings.h"

class RdpClient;

// Gives an RDP session the same lifecycle every other pane has (state,
// connect/disconnect, errors - so auto-reconnect, the status strip and tab
// icon all work unchanged). Like VncTransport there is no byte stream: the
// pixels go from client() straight to the pane's RdpWidget.
class RdpTransport : public Transport
{
    Q_OBJECT

public:
    explicit RdpTransport(RdpConnectionSettings settings, QObject *parent = nullptr);
    // Panes share one settings object across reconnects, so credentials typed
    // at the prompt are remembered (in memory only) instead of asked for again
    // every time the session drops and comes back.
    explicit RdpTransport(std::shared_ptr<RdpConnectionSettings> settings, QObject *parent = nullptr);

    void connectToHost() override;
    void disconnectFromHost() override;
    void resize(int, int) override {}

    RdpClient *client() const { return m_client; }

public slots:
    void write(const QByteArray &) override {}

private:
    std::shared_ptr<RdpConnectionSettings> m_settings;
    RdpClient *m_client = nullptr;
};
