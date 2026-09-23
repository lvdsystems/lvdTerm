# Third-party notices

lvdterm is licensed under the GNU General Public License, version 3 or later
(see `LICENSE`). That choice is a consequence of the components below - see
the explanation after the list.

## Qt 6

- **License:** GNU Lesser General Public License v3 (LGPLv3), some modules
  additionally under GPLv2/GPLv3.
- **Used as:** dynamically-linked DLLs (`Qt6Core.dll`, `Qt6Gui.dll`,
  `Qt6Widgets.dll`, `Qt6Network.dll`, `Qt6SerialPort.dll`, ...), bundled next
  to `lvdterm.exe` by `windeployqt`. Not statically linked - LGPLv3's
  relink requirement is satisfied by ordinary DLL replacement.
- **Source / full license:** <https://www.qt.io/> and the license files
  shipped with the Qt SDK.

## QTermWidget / Konsole core (vendored)

- **License:** GNU General Public License v2 or later (GPL-2.0-or-later).
- **Used as:** a stripped-down, patched-for-Windows copy of Konsole's
  terminal-emulation core (VT parser, screen buffer, history, keyboard
  translation), vendored and statically compiled into `lvdterm.exe` - see
  `third_party/qtermwidget/VENDORING.md`.
  Copyright the original Konsole/QTermWidget authors (Lars Doelle, Robert
  Knight, and others - see individual file headers).
- **Full license:** `third_party/qtermwidget/LICENSE`.

## libssh2 (vendored)

- **License:** BSD-3-Clause.
- **Used as:** statically compiled into `lvdterm.exe`, providing the SSH/SFTP
  transport - see `third_party/libssh2/VENDORING.md`.
  Copyright Sara Golemon and other libssh2 contributors - see
  `third_party/libssh2/COPYING`.
- **Full license:** `third_party/libssh2/COPYING`.

## OpenSSL (vendored)

- **License:** Apache License 2.0.
- **Used as:** a statically-linked static build providing libssh2's crypto
  backend (chosen over WinCNG/mbedTLS specifically for ed25519/
  curve25519-sha256 support) - see `third_party/openssl-prebuilt/BUILDING.md`.
- **Full license:** `third_party/openssl-prebuilt/LICENSE.txt`.

## Tabler Icons (app icon)

- **License:** MIT.
- **Used as:** the app icon (`src/resources/icons/app_icon.svg`, adapted -
  recolored to white and given a solid background for taskbar/shell
  legibility, see the SVG's own comment) is the "terminal-2" icon from the
  Tabler Icons set, <https://tabler.io/icons/icon/terminal-2>, source
  <https://github.com/tabler/tabler-icons>.
- **Full license:** <https://github.com/tabler/tabler-icons/blob/main/LICENSE>
  (MIT - copyright the Tabler Icons contributors).

## Why GPL-3.0-or-later for lvdterm as a whole

The vendored QTermWidget/Konsole core is GPL - specifically "version 2, or
(at your option) any later version" per its own file headers - and it is
compiled directly into `lvdterm.exe` (not used as a separate process or a
dynamically-loaded, independently-replaceable library), which makes the
combined binary a GPL derivative work.

Separately, `lvdterm.exe` also statically links OpenSSL (Apache-2.0). The
Free Software Foundation's own license-compatibility guidance states that
**Apache License 2.0 is not compatible with GPL version 2** (its
patent-termination and indemnification clauses conflict with GPLv2's terms),
but that this was deliberately fixed in **GPL version 3**, which is
compatible with Apache-2.0. Because QTermWidget's license permits moving to
"any later version," lvdterm as a combined work is only actually consistent
under **GPL-3.0-or-later** - plain GPL-2.0 would combine two components
(QTermWidget and OpenSSL) whose licenses do not permit that combination.

GPLv3 is also compatible with Qt's LGPLv3, and BSD-3-Clause (libssh2) is
permissive and compatible with everything above.
