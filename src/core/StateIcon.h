#pragma once

#include <QColor>
#include <QIcon>

#include "Transport.h"

// The colored-dot visual language used everywhere a Transport::State needs
// a glance-able indicator: MainWindow's tab icons and TerminalView's
// per-pane status bar both draw the same colors, via the same function.
namespace StateIcon
{
QColor color(Transport::State state);
QIcon icon(Transport::State state); // a 12x12 colored dot, e.g. for the pane status strip
// A tab's LED: the same dot centered on a 16x16 canvas, with a glowing halo
// around it when `activity` (the tab has output the user hasn't seen).
QIcon tabIcon(Transport::State state, bool activity);
} // namespace StateIcon
