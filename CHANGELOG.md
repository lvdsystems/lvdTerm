# Changelog

## 1.0

### Bugfix
- Closing any SSH or SFTP connection could kill the other live connections on
  Windows (a Winsock reference-count bug). It showed up as panes reconnecting and
  the SFTP dock dropping, for instance when switching between panes of different hosts.
- SFTP dock: follows its terminal again. It notices a dead connection (also while
  idle), reconnects by itself, reconnects as soon as its terminal does, and picks up
  a password corrected at the terminal's prompt. A login that was refused or a host
  key that was rejected isn't retried on its own; Refresh tries again.
- New host: one host key prompt, not one each for the terminal and the SFTP dock.
- Freezes: a burst of terminal output, a large paste into a local shell that isn't
  reading, closing a local shell, and closing or reconnecting an RDP pane could each
  block the whole window, the last two possibly for good. Connecting to a host that
  accepts the connection but never answers now times out, and so does a dead SFTP
  connection, instead of waiting forever.
- Connection prompts (host key, password, RDP/VNC credentials) can no longer open
  behind other windows and leave the main window ignoring clicks.
- SSH X11 forwarding: fixed memory corruption when a remote X program started while
  others were running, and a stalled paste into a busy remote program.

### New Features
- If the window ever stops responding for 10 seconds, where it is stuck is written
  to hang.log in the application data folder.

## 0.7

### New Features
- **VNC connections**: Connection > New VNC Connection..., or a saved connection of
  type VNC, shown in a pane. lvdterm's own VNC client: VNC password login, Tight
  (zlib and JPEG), ZRLE, Hextile, CopyRect and Raw encodings, mouse, keyboard and
  two-way clipboard text (copied text is offered to the remote machine as soon as it
  is copied, so Ctrl+V there pastes it). The picture is scaled to fit; Shift+right-click opens the
  pane menu (a plain right-click goes to the remote machine).
- **RDP connections**: Connection > New RDP Connection..., or a saved connection of
  type RDP, shown in a pane (NLA/TLS login, mouse and keyboard, scaled to fit).
  Built on the vendored FreeRDP library, with text clipboard in both directions
  (Ctrl+C / Ctrl+V work across the two machines). No audio or drive redirection
  yet, and the server certificate is not verified.
- **Detach a tab into its own window**: tab right-click > "Detach Tab to New
  Window", keeping its live connections; "Move Tab to Main Window" puts it back.
  Detached tabs are restored as ordinary tabs on the next launch.
- **SSH X11 forwarding**: a per-connection "Forward X11" option (on by default)
  that displays remote graphical programs on the local X server named by $DISPLAY.
  lvdterm includes no X server: Linux uses the desktop's own; on Windows install
  one such as VcXsrv (the connection form links to it).

### Bugfix
- SFTP dock: switching between panes of different SSH hosts logged in to the new
  host again every time (and dropped the previous login). Sessions now stay
  connected while their pane is open, so switching back is instant; a closed pane's
  session is disconnected. Also fixed upload requests being connected once more on
  every switch.
- RDP: credentials typed at the prompt are remembered for that pane's reconnects
  instead of being asked for again each time the session drops, and the pane now
  says why the server ended a session (e.g. another connection with the same
  account took it over).

### Notes
- New third-party components, all statically linked: FreeRDP (Apache-2.0), zlib
  (Windows build) and stb_image (MIT / public domain). See THIRD_PARTY_LICENSES.md.
- Not supported yet: VNC encrypted logins (VeNCrypt/TLS, e.g. security type 18),
  RDP virtual channels.

## 0.6

### New Features
- Settings > Keyboard Shortcuts: a dedicated page listing every shortcut, with
  change / clear / reset per command, and custom shortcuts that type a piece of
  text into the active terminal. Changes apply immediately and persist.
- Copy / Paste on Ctrl+Shift+C / Ctrl+Shift+V (plain Ctrl+C still reaches the
  running program).
- Find in scrollback on Ctrl+Shift+F, now also searching hex-viewer panes.
- Drag a pane by its title strip to move it (split on an edge, swap in the
  middle); "Respawn Terminal" and "Change Settings..." in the pane menu.

### Bugfix
- Ctrl+F did nothing in a terminal pane: it was bound twice, which Qt treats as
  ambiguous and fires neither. There is now one binding per command.
- Linux (GNOME/Wayland): windows had no minimize/maximize/close buttons when
  started from a shell with a stale session D-Bus address (the bus had restarted
  since the shell began, so Qt refused to connect and never learned the button
  layout). The address is now repaired at startup.

## 0.5

### New Features
- Local terminal tabs (cmd/PowerShell/shell), with shortcuts to open and close
  windows.
- A sub-menu inside each window with Split and Close.
- SFTP: create new files, a built-in file editor with syntax highlighting, and
  find; Del key and media/back-forward button navigation in the file list.
- Copy on select in the terminal.
- Connection folder selection and management reworked.
- UI/UX refresh across the application.

### Bugfix
- SFTP pane layout at small sizes.

## 0.4

### New Features
- Linux support: builds and runs on Linux, packaged as a .deb and an AppImage.

## 0.3.2

### New Features
- SSH wrong password: in case of wrong password inserted popups to ask it again
- Connection dialog polish

### Bugfix
- Allow not to specify any password for SSH

## 0.3.1

### Bugfix
- SFTP upload could fail due to socket timeout handling


## 0.3

### New features
- a status bar context menu on every pane (Reconnect, Clear
  Screen, Clear Buffer).
- the terminal's own right-click context menu is gone -
  right-click now pastes directly, as it did before that menu existed.
- per-connection session logging to a file, with an option to
  prefix each line with a timestamp.
- a per-connection viewer setting - Terminal (default) or Hex, for inspecting raw/binary data streams.
- byte/word/dword grouping in the hex viewer.


### Bugfix
- improved auto-reconnect and reconnect
- more robust async use of SSH workers
- SFTP uploads/downloads could spuriously fail (reported as "...: OK")
  on any transfer slower than 15s - a socket timeout meant to bound
  connecting was being left on for the whole session



## 0.2

- Initial public release.
