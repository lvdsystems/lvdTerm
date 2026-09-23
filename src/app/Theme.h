#pragma once

#include <QString>

// Applies the app-wide widget palette for AppSettings::theme() ("Dark"
// or "Light") on top of the Fusion style set in main.cpp. This is
// separate from ColorSchemes (src/terminal/), which only affects the
// terminal panes' own text/background colors.
namespace Theme
{

void apply(const QString &name);

}
