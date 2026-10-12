#pragma once

// The platform's "local shell on a pseudo-terminal" Transport - callers
// (MainWindow) just say LocalTransport and never need to know which.
#ifdef _WIN32
#include "transports/conpty/ConPtyTransport.h"
using LocalTransport = ConPtyTransport;
#else
#include "transports/pty/PtyTransport.h"
using LocalTransport = PtyTransport;
#endif
