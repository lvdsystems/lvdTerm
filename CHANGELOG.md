# Changelog

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
