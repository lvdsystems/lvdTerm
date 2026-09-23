// Written from scratch for lvdterm — not part of upstream QTermWidget.
//
// Upstream's real qtermwidget.h is the pty-aware facade widget (owns a
// Session, spawns shells, etc.) that lvdterm does not vendor (see
// VENDORING.md). TerminalDisplay.h only needs two symbols that upstream
// happened to expose off that facade class: the ScrollBarPosition enum
// and the KeyboardCursorShape alias. This header provides just those, so
// TerminalDisplay.{h,cpp} can be used completely unmodified.
//
// lvdterm's own terminal widget lives in src/terminal/TerminalView.
#pragma once

#include "Emulation.h"
#include "qtermwidget_interface.h"

class QTermWidget {
public:
    enum ScrollBarPosition {
        NoScrollBar = QTermWidgetInterface::NoScrollBar,
        ScrollBarLeft = QTermWidgetInterface::ScrollBarLeft,
        ScrollBarRight = QTermWidgetInterface::ScrollBarRight
    };

    using KeyboardCursorShape = Konsole::Emulation::KeyboardCursorShape;
};
