# Vendored QTermWidget core

Source: https://github.com/lxqt/qtermwidget.git
See `../QTERMWIDGET_UPSTREAM.txt` for the exact commit.
License: GPL-2.0-or-later (see `LICENSE` in this directory, and individual
file headers - "version 2, or (at your option) any later version"). This is
one of the reasons lvdterm as a whole is licensed GPL-3.0-or-later rather
than GPL-2.0 - see `../../THIRD_PARTY_LICENSES.md` for the full reasoning
(the short version: GPLv2 is not compatible with the Apache-2.0-licensed
OpenSSL that's also statically linked into lvdterm.exe; GPLv3 fixed that
incompatibility, and this file's "or later" clause is what makes moving to
GPLv3 possible).

## What was vendored

Only the pure terminal-emulation core from `lib/`: `Emulation`,
`Vt102Emulation`, `Screen`, `ScreenWindow`, `History`/`HistorySearch`,
`KeyboardTranslator`, `Filter`/`Hyperlink`, `TerminalCharacterDecoder`,
`TerminalDisplay`, `konsole_wcwidth`, `tools`, `BlockArray`, and the small
data headers (`Character.h`, `CharacterColor.h`, `LineFont.h`,
`DefaultTranslatorText.h`, `ExtendedDefaultTranslator.h`,
`qtermwidget_interface.h`). These files are otherwise unmodified from
upstream except where noted below.

## What was intentionally dropped

- `Pty.cpp/.h`, `kpty*.cpp/.h`, `kprocess.cpp/.h` — POSIX-only local pty/
  process spawning (`openpty`, `forkpty`, `termios`, `ioctl`). Not portable
  to Windows and not needed: every lvdterm transport (SSH/Telnet/Serial) is
  a byte stream, not a local pty. A future local-shell tab would use
  Windows ConPTY, a wholly different API, so this code wouldn't have helped
  there either.
- `Session.cpp/.h` — the Pty-owning glue class. Replaced by
  `src/terminal/TerminalSession` (see that file), which wires
  `Emulation`/`TerminalDisplay` to our `Transport` interface instead of a
  `Pty`. Modeled on the signal/slot wiring in upstream `Session::addView`
  and `qtermwidget.cpp`'s constructor.
- `qtermwidget.cpp/.h` — the public facade widget (owns a `Session`,
  exposes shell-spawning API). Not applicable without a pty. Replaced by
  our own `TerminalView` (see `src/terminal/`).
- `ColorScheme.cpp/.h`, `ColorTables.h`, `SearchBar.cpp/.h/.ui` — theming
  and scrollback-search UI, replaced by lvdterm's own `src/terminal/
  ColorSchemes.cpp` and `TerminalView`'s built-in find bar respectively.
  `TerminalDisplay` has its own built-in `base_color_table` and renders
  fine without a `ColorSchemeManager`.
- `lib/kb-layouts/`, `lib/color-schemes/`, `lib/default.keytab`,
  `lib/translations/` — external data files for extra keyboard layouts,
  color schemes and translations. Not needed yet: `KeyboardTranslator`
  falls back to the compiled-in `DefaultTranslatorText.h` when no
  `default.keytab` is found on disk.

## Patches applied to vendored files

- **`Emulation.cpp`**: removed `#include "Session.h"`. It was a vestigial,
  unused include (the only symbol from `Session.h`'s enum, `NOTIFYACTIVITY`,
  is actually defined in `Emulation.h` itself).
- **`BlockArray.cpp`**: this implements optional disk-swap for scrollback
  history using `mmap()`/`munmap()`/`getpagesize()` — none of which mingw
  provides (no `<sys/mman.h>`). Rewritten to use `lseek`/`read` into a heap
  buffer instead of `mmap`, and a fixed 4096-byte page size instead of
  `getpagesize()`. `lseek`/`read`/`write`/`close`/`dup`/`ftruncate` are all
  provided by mingw-w64's `<io.h>`/`<unistd.h>`, so no other changes were
  needed. Behavior (a temp-file-backed ring buffer of history blocks) is
  unchanged; only the read path no longer memory-maps the file.

- **`TerminalDisplay.cpp`** (`wheelEvent()` no longer sends synthetic
  Up/Down keys): upstream's wheel handling only scrolls the real scrollbar
  when `_scrollBar->maximum() > 0`; 

- **`Emulation.h`/`.cpp`** (`clearScreenAndHistory()`, new method)

## Written from scratch (not from upstream)

- **`qtermwidget_export.h`**: upstream generates this via CMake's
  `generate_export_header()` for a shared-library build. lvdterm builds
  this core as a static library baked into one executable, so this is just
  `#define QTERMWIDGET_EXPORT` (no dllexport/dllimport boundary needed).
- **`qtermwidget.h`**: upstream's real `qtermwidget.h` is the pty-aware
  facade class we dropped (see above). `TerminalDisplay.h` only needed two
  symbols from it — the `ScrollBarPosition` enum and the
  `KeyboardCursorShape` alias — so this file is a tiny compatibility shim
  providing just those, not a port of the facade.
