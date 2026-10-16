# Changelog

## Unreleased

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
