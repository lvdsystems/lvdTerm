#pragma once

#include <QStringList>

// Registers a curated set of embedded keyboard/terminal-emulation profiles
// with the vendored KeyboardTranslatorManager (third_party/qtermwidget/
// KeyboardTranslator.h), so Emulation::setKeyBindings(name) (already called
// once per TerminalSession) can select between them per connection - see
// ConnectionProfile::keyboardProfile / AppSettings::defaultKeyboardProfile.
//
// Registering one named "default" is what actually fixes arrow/Home/End/
// Insert/Delete/F-key support: KeyboardTranslatorManager::
// defaultTranslator() looks up exactly that name before ever falling back
// to DefaultTranslatorText.h's 2-line emergency stub (which only binds
// Tab) - lvdterm never shipped a real default.keytab on disk (see
// third_party/qtermwidget/VENDORING.md), so every connection was silently
// running on that stub until this registers a real one in its place.
namespace KeyboardProfiles
{
// Call once at startup (main.cpp), before any TerminalSession exists.
void registerAll();

// Registration names (also what's stored in ConnectionProfile::
// keyboardProfile / AppSettings::defaultKeyboardProfile, and what
// Emulation::setKeyBindings() expects) - "default" always included first.
QStringList names();
} // namespace KeyboardProfiles
