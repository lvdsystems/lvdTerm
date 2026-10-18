#pragma once

#include <QString>

// How to open a VNC (RFB) connection. Doubles as the persisted profile
// payload for this connection type (see ConnectionProfile).
struct VncConnectionSettings
{
    QString host;
    quint16 port = 5900; // display :0; display :N is 5900 + N
    // Saved like the SSH password (encrypted at rest). Empty + a server
    // that asks for one = prompt at connect time.
    QString password;
};
