#pragma once

#include <functional>
#include <optional>

#include <QWidget>

#include "TerminalDisplay.h"
#include "core/ConnectionProfile.h"
#include "core/Transport.h"
#include "transports/ssh/SshConnectionSettings.h"

using Konsole::TerminalDisplay; // TerminalDisplay lives in namespace Konsole

class Transport;
class TerminalSession;
class HexDumpWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QFile;

// The widget a tab/split pane actually shows: a TerminalDisplay (vendored
// QTermWidget core) driven by a TerminalSession, fed by a Transport handed
// in from outside. This is lvdterm's replacement for upstream's
// QTermWidget facade (see third_party/qtermwidget/VENDORING.md) — it knows
// nothing about shells or ptys, only about Transport.
//
// Also owns auto-reconnect: if the Transport drops to
// Disconnected/Error while this view is still alive - which, since there
// is no "disconnect but keep the pane open" action, can only mean the
// far end/link actually died, not that the user asked to close anything -
// it swaps in a fresh Transport (built by `recreate`) after a backoff
// delay (see ReconnectPolicy) and shows a small overlay with a countdown
// and a Cancel button. MainWindow never talks to the Transport directly;
// it connects to this class's own stateChanged/errorOccurred signals,
// which keep working across a transport swap.
class TerminalView : public QWidget
{
    Q_OBJECT

public:
    using TransportFactory = std::function<Transport *()>;

    // `recreate` must build a fresh, equivalent Transport (same settings,
    // not yet connected) - used both by MainWindow when splitting
    // ("duplicate this connection") and internally when reconnecting.
    // `sshSettings`, when present, feeds the SFTP dock: it
    // connects a second session to the same host/credentials as
    // whichever SSH pane currently has focus. `sessionSnapshot`, when
    // present, is "how to reconnect this pane" for session save/restore
    // (SessionStore) - std::nullopt for panes that are never persisted
    // (loopback).
    TerminalView(Transport *transport, TransportFactory recreate, std::optional<SshConnectionSettings> sshSettings,
                 std::optional<ConnectionProfile> sessionSnapshot, QWidget *parent = nullptr);
    ~TerminalView() override;

    void connectToHost();
    void setAutoReconnectEnabled(bool enabled) { m_autoReconnect = enabled; }
    bool autoReconnectEnabled() const { return m_autoReconnect; }

    // Explicitly resizes the display to `size` and forces it to recompute
    // its column/line count. Needed after session restore: a hidden
    // QTabWidget page's actual widget geometry is never kept in sync with
    // the current page's (confirmed empirically - QStackedWidget does not
    // resize non-current pages to match), so a restored pane that isn't
    // the tab that ends up current stays at whatever size it happened to
    // have right after construction - which, before the window is even
    // shown for the first time, is a genuinely tiny placeholder size, not
    // just a stale-but-reasonable one. `size` should be the visible tab's
    // real size, read only after the window has actually been shown (see
    // MainWindow::showEvent()) - anything read before that reflects
    // provisional pre-show layout, not the window manager's true final
    // geometry.
    void ensureCorrectSize(const QSize &size);

    void showFindBar(); // Ctrl+F, also bound directly on this widget

    // Used by MainWindow's "Terminal" menu (enabled only while a terminal
    // pane has focus - see MainWindow::createMenus()) and, for Paste, by
    // right-click (see the constructor's configureRequest hookup).
    void copySelection();
    void pasteFromClipboard();
    void clearScreen(); // Emulation::clearEntireScreen() - visible screen only, pushed into history
    void clearBuffer(); // Emulation::clearScreenAndHistory() - screen + all scrollback + homes the cursor

    bool isLogging() const { return m_logFile != nullptr; }
    // no-op (silently) if the file can't be opened. includeTimestamps
    // prefixes each line with "[HH:mm:ss.zzz] " as it's written - see
    // onDataForLogging().
    void startLogging(const QString &path, bool includeTimestamps = false);
    void stopLogging();

    Transport::State transportState() const { return m_transport->state(); }
    TransportFactory recreateFactory() const { return m_recreate; }
    const std::optional<SshConnectionSettings> &sshSettings() const { return m_sshSettings; }
    const std::optional<ConnectionProfile> &sessionSnapshot() const { return m_sessionSnapshot; }

signals:
    // Forwarded from whichever Transport is currently active - stable
    // across a reconnect's transport swap, unlike connecting to the
    // Transport's own signals directly.
    void stateChanged(Transport::State state);
    void errorOccurred(const QString &message);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void hookTransportSignals();
    void replaceTransport(Transport *newTransport);
    void applyAppearance(); // font + color scheme, from AppSettings

    void onStateChanged(Transport::State state);
    void scheduleReconnect();
    void showManualReconnectPrompt(); // auto-reconnect disabled: still always show a way back
    void doReconnectNow();
    void cancelReconnect();
    void tickCountdown();
    void updateOverlayText();
    void ensureOverlay();
    void showOverlay();
    void hideOverlay();

    void ensureFindBar();
    void performSearch(bool forwards);
    void closeFindBar();
    void onDataForLogging(const QByteArray &data);
    void updateStatusBar(Transport::State state);

    TerminalDisplay *m_display = nullptr;
    TerminalSession *m_session = nullptr;
    Transport *m_transport = nullptr;
    TransportFactory m_recreate;
    std::optional<SshConnectionSettings> m_sshSettings;
    std::optional<ConnectionProfile> m_sessionSnapshot;

    // ConnectionProfile::Viewer::Hex: bytes bypass m_session/m_display
    // (the VT100 emulation) entirely and go straight to m_hexView
    // instead - see the constructor and hookTransportSignals(). m_display
    // still exists either way (simpler than conditionally-constructed
    // members), it's just never attached to a session or added to the
    // layout in this mode.
    const bool m_hexMode = false;
    HexDumpWidget *m_hexView = nullptr;

    bool m_autoReconnect = true;
    int m_attempt = 0;
    int m_secondsRemaining = 0;
    bool m_gaveUp = false;
    QTimer *m_reconnectTimer = nullptr; // single-shot, fires the actual reconnect attempt
    QTimer *m_countdownTimer = nullptr; // 1 Hz, just updates the overlay label

    QWidget *m_overlay = nullptr;
    QLabel *m_overlayLabel = nullptr;
    QPushButton *m_overlayButton = nullptr; // "Cancel" while counting down, "Retry Now" after giving up

    QWidget *m_findBar = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QLabel *m_findStatusLabel = nullptr;
    bool m_hasMatch = false;
    int m_matchStartColumn = 0;
    int m_matchStartLine = 0;
    int m_matchEndColumn = 0;
    int m_matchEndLine = 0;

    QFile *m_logFile = nullptr;
    bool m_logIncludeTimestamps = false;
    bool m_logAtLineStart = true; // next byte written begins a fresh line - see onDataForLogging()

    // Always visible (unlike m_findBar), a thin strip at the top of the
    // pane showing this pane's own live connection state - see
    // updateStatusBar()/StateIcon.h.
    QLabel *m_statusIconLabel = nullptr;
    QLabel *m_statusTextLabel = nullptr;

    // True for the duration of one in-progress mouse-driven selection (see
    // TerminalDisplay::isBusySelecting) - gates the auto-copy-on-select
    // hookup so it writes to the clipboard once, when the selection
    // settles, rather than on every intermediate mouse-move.
    bool m_selectionBusy = false;
};
