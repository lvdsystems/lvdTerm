#pragma once

#include <QString>

// How to open an RDP connection. Doubles as the persisted profile payload
// for this connection type (see ConnectionProfile).
struct RdpConnectionSettings
{
    QString host;
    quint16 port = 3389;
    QString username;
    QString domain;
    // Saved like the SSH password (encrypted at rest). Empty = prompt at
    // connect time (the server asks for credentials).
    QString password;
    // The remote desktop's size, fixed for the session (the pane scales it
    // to fit).
    int desktopWidth = 1280;
    int desktopHeight = 800;
};
