#pragma once

#include <QStringList>

#include "CharacterColor.h"

// A handful of built-in terminal color schemes, applied via
// TerminalDisplay::setColorTable(). The upstream ColorScheme/
// ColorSchemeManager classes (which parse .colorscheme files) were
// deliberately not vendored - see third_party/qtermwidget/VENDORING.md -
// so these are plain hardcoded tables instead, in the same TABLE_COLORS
// layout as TerminalDisplay.cpp's own base_color_table: [0]/[1] are the
// default foreground/background, [2..9] the 8 ANSI colors, [10]/[11] the
// bold default fore/back, [12..19] the 8 bright ANSI colors.
namespace ColorSchemes
{

QStringList names();

// Returns TABLE_COLORS (20) entries. Falls back to the first scheme in
// names() for an unrecognized name.
const Konsole::ColorEntry *table(const QString &name);

} // namespace ColorSchemes
