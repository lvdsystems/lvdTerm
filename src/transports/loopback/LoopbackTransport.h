#pragma once

#include "core/Transport.h"

// Bring-up/testing transport: echoes back whatever is written to it, with
// no remote end at all. Useful to validate the vendored terminal-emulation
// core end to end independent of any real transport (Serial/Telnet/SSH).
// A real shell could be attached later the same way by swapping
// this out for e.g. a ConPtyTransport.
class LoopbackTransport : public Transport
{
    Q_OBJECT

public:
    using Transport::Transport;

    void connectToHost() override { setState(State::Connected); }
    void disconnectFromHost() override { setState(State::Disconnected); }
    void resize(int /*cols*/, int /*rows*/) override {}

public slots:
    void write(const QByteArray &data) override { emit readyRead(data); }
};
