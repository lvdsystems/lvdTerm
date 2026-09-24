#include "TerminalView.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

#include "HexDumpWidget.h"
#include "HistorySearch.h"
#include "ScreenWindow.h"
#include "TerminalSession.h"
#include "core/AppSettings.h"
#include "core/ReconnectPolicy.h"
#include "core/StateIcon.h"
#include "core/Transport.h"
#include "ColorSchemes.h"

TerminalView::TerminalView(Transport *transport, TransportFactory recreate, std::optional<SshConnectionSettings> sshSettings,
                           std::optional<ConnectionProfile> sessionSnapshot, QWidget *parent)
    : QWidget(parent)
    , m_display(new TerminalDisplay(this))
    , m_session(new TerminalSession(this))
    , m_transport(transport)
    , m_recreate(std::move(recreate))
    , m_sshSettings(std::move(sshSettings))
    , m_sessionSnapshot(std::move(sessionSnapshot))
    , m_hexMode(m_sessionSnapshot && m_sessionSnapshot->viewer == ConnectionProfile::Viewer::Hex)
    , m_reconnectTimer(new QTimer(this))
    , m_countdownTimer(new QTimer(this))
{
    m_transport->setParent(this);

    if (m_hexMode) {
        m_hexView = new HexDumpWidget(this);
        // m_display is still constructed unconditionally above (simpler
        // than conditionally-constructed members) but never added to the
        // layout in this mode - reported directly as a visual glitch (an
        // unstyled blank rectangle): a child widget that's never put in
        // a layout still inherits visibility from its shown parent and
        // paints itself at whatever default/stale geometry it happens to
        // have, it doesn't just stay invisible on its own.
        m_display->hide();
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *statusBar = new QWidget(this);
    statusBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed); // never taller than its own contents
    auto *statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(6, 2, 6, 2);
    m_statusIconLabel = new QLabel(statusBar);
    m_statusTextLabel = new QLabel(statusBar);
    m_statusTextLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    statusLayout->addWidget(m_statusIconLabel);
    statusLayout->addWidget(m_statusTextLabel, 1);

    statusBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(statusBar, &QWidget::customContextMenuRequested, this, [this, statusBar](const QPoint &pos) {
        QMenu menu(this);
        // Available regardless of the overlay's own state - a second,
        // direct path to reconnect that doesn't depend on the
        // auto-reconnect backoff/overlay machinery working correctly -
        // *except* while a connection attempt is already in progress.
        // Reported directly (a real crash, not just a bad idea): tearing
        // down a Transport that's mid-connect can leave it with no way
        // to safely/promptly stop whatever blocking or in-flight
        // operation it's in the middle of - true of any transport, not
        // just one kind - so the structural fix is to never let this be
        // triggered while Connecting, for every transport uniformly,
        // rather than chasing down and bounding each transport's own
        // internal blocking operations one at a time.
        QAction *reconnectAction = menu.addAction(QStringLiteral("Reconnect"));
        reconnectAction->setEnabled(m_transport->state() != Transport::State::Connecting);
        connect(reconnectAction, &QAction::triggered, this, &TerminalView::doReconnectNow);
        // Clear Screen/Clear Buffer act on the VT100 emulation, which
        // hex mode doesn't have (see m_hexMode) - nothing for them to do.
        if (!m_hexMode) {
            menu.addSeparator();
            connect(menu.addAction(QStringLiteral("Clear Screen")), &QAction::triggered, this, &TerminalView::clearScreen);
            connect(menu.addAction(QStringLiteral("Clear Buffer")), &QAction::triggered, this, &TerminalView::clearBuffer);
        }
        menu.exec(statusBar->mapToGlobal(pos));
    });

    layout->addWidget(statusBar); // stretch 0 (default): fixed to its sizeHint, above the display
    layout->addWidget(m_hexMode ? static_cast<QWidget *>(m_hexView) : static_cast<QWidget *>(m_display), 1); // stretch 1: everything not needed by the status bar
    updateStatusBar(m_transport->state());

    if (!m_hexMode) {
        applyAppearance();
        m_display->setScrollBarPosition(QTermWidget::ScrollBarRight);
        m_display->setKeyboardCursorShape(QTermWidget::KeyboardCursorShape::BlockCursor);
        m_display->setBlinkingCursor(true);
    }

    if (!m_hexMode) {
        // Auto-copy on select: TerminalDisplay's own setSelection() (called
        // internally whenever the mouse selection changes) only ever writes to
        // QClipboard::Selection - the X11 "primary selection", which doesn't
        // exist on Windows (QClipboard::supportsSelection() is false there), so
        // that call is silently a no-op on this platform. copyAvailable(bool)
        // fires on *every* selection change though, including every intermediate
        // mouseMoveEvent while a drag is still in progress (extendSelection()
        // calls setSelectionStart/setSelectionEnd on each move) - copying to the
        // real, OS-level clipboard on every one of those (a synchronous call
        // that other apps, e.g. Windows' own clipboard history, can hook and
        // slow down further) made drag-selecting visibly lag the whole GUI.
        // isBusySelecting(bool) brackets exactly one in-progress
        // drag/double-click/triple-click selection (true at its start, false
        // once the mouse is released - TerminalDisplay.cpp's mouseReleaseEvent/
        // mouseMoveEvent/mouseTripleClickEvent), so gate the frequent signal on
        // it and instead do the actual copy once, when the selection settles.
        connect(m_display, &TerminalDisplay::isBusySelecting, this, [this](bool busy) {
            m_selectionBusy = busy;
            if (!busy)
                m_display->copyClipboard(); // no-op if selection ended up empty (e.g. a plain click)
        });
        connect(m_display, &TerminalDisplay::copyAvailable, this, [this](bool available) {
            if (available && !m_selectionBusy)
                m_display->copyClipboard();
        });

        // Right-click paste: configureRequest() is TerminalDisplay's own
        // "user right-clicked (and no mouse-aware program has the mouse right
        // now, or Shift is held)" signal - the terminal's own context menu
        // (added in an earlier round: Clear Buffer/Clear Screen/Copy/Paste)
        // is removed per direct request, replaced with right-click pasting
        // directly. MainWindow's top-level "Terminal" menu still offers all
        // four actions, kept as-is.
        connect(m_display, &TerminalDisplay::configureRequest, this, [this](const QPoint &) { m_display->pasteClipboard(); });

        m_session->setTransport(m_transport);
        // See KeyboardProfiles.h/ConnectionProfile::keyboardProfile: an
        // explicit per-connection choice wins; otherwise fall back to the
        // global default (itself "default" unless changed in Settings).
        const QString keyboardProfile = (m_sessionSnapshot && !m_sessionSnapshot->keyboardProfile.isEmpty()) ? m_sessionSnapshot->keyboardProfile
                                                                                                               : AppSettings::instance().defaultKeyboardProfile();
        m_session->setKeyboardProfile(keyboardProfile);
        m_session->attachView(m_display);

        setFocusProxy(m_display);
    } else {
        setFocusProxy(m_hexView);
    }

    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &TerminalView::doReconnectNow);
    connect(m_countdownTimer, &QTimer::timeout, this, &TerminalView::tickCountdown);

    // Live-apply Settings dialog changes to every already-open pane.
    connect(&AppSettings::instance(), &AppSettings::terminalAppearanceChanged, this, &TerminalView::applyAppearance);

    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this, &TerminalView::showFindBar);

    hookTransportSignals();

    // Auto-start session logging for a saved connection that asks for it
    // (ConnectionEditDialog), rather than needing MainWindow's ad hoc
    // "Log Session to File..." action every time. Starts immediately
    // (not gated on reaching Connected) so nothing - including any
    // pre-connection banner - is missed.
    if (m_sessionSnapshot && m_sessionSnapshot->logSessionToFile && !m_sessionSnapshot->logFilePath.isEmpty())
        startLogging(m_sessionSnapshot->logFilePath, m_sessionSnapshot->logIncludeTimestamps);
}

void TerminalView::applyAppearance()
{
    m_display->setVTFont(AppSettings::instance().terminalFont());
    m_display->setColorTable(ColorSchemes::table(AppSettings::instance().colorScheme()));
}

TerminalView::~TerminalView() = default;

void TerminalView::connectToHost()
{
    m_transport->connectToHost();
}

void TerminalView::copySelection()
{
    m_display->copyClipboard();
}

void TerminalView::pasteFromClipboard()
{
    m_display->pasteClipboard();
}

void TerminalView::clearScreen()
{
    m_session->emulation()->clearEntireScreen();
}

void TerminalView::clearBuffer()
{
    // Clears the current screen too and homes the cursor, not just
    // scrollback - see Emulation::clearScreenAndHistory()'s comment for
    // why clearHistory() alone left stale content/cursor position behind.
    m_session->emulation()->clearScreenAndHistory();
}

void TerminalView::ensureCorrectSize(const QSize &size)
{
    resize(size);

    if (m_hexMode) {
        // HexDumpWidget has no column/line concept to re-derive from
        // geometry (unlike TerminalDisplay below) - an ordinary resize is
        // enough.
        m_hexView->resize(size);
        return;
    }

    // resize() updates real widget geometry (unlike a synthetic
    // QResizeEvent alone, which would just re-derive columns/lines from
    // whatever geometry the display already has - insufficient here since
    // a hidden tab page's geometry can genuinely be stale/wrong, not just
    // un-notified). TerminalDisplay::resizeEvent() ignores its event
    // argument entirely and just recomputes columns/lines from the
    // widget's actual current geometry, so resize() alone triggers the fix
    // whenever the size actually changes; the explicit event covers the
    // (harmless either way) case where it happens to already match.
    m_display->resize(size);
    QResizeEvent event(m_display->size(), QSize());
    QCoreApplication::sendEvent(m_display, &event);
}

void TerminalView::hookTransportSignals()
{
    connect(m_transport, &Transport::stateChanged, this, &TerminalView::stateChanged);
    connect(m_transport, &Transport::stateChanged, this, &TerminalView::onStateChanged);
    connect(m_transport, &Transport::errorOccurred, this, &TerminalView::errorOccurred);
    connect(m_transport, &Transport::readyRead, this, &TerminalView::onDataForLogging);

    // Hex mode: bytes go straight to the dump view instead of through
    // m_session/the VT100 emulation (see m_hexMode's declaration).
    if (m_hexMode)
        connect(m_transport, &Transport::readyRead, m_hexView, &HexDumpWidget::appendData);
}

void TerminalView::replaceTransport(Transport *newTransport)
{
    Transport *old = m_transport;
    disconnect(old, nullptr, this, nullptr);
    if (m_hexMode)
        disconnect(old, nullptr, m_hexView, nullptr); // hookTransportSignals() connects readyRead straight to m_hexView, not `this`

    m_transport = newTransport;
    m_transport->setParent(this);
    if (!m_hexMode)
        m_session->setTransport(m_transport);
    hookTransportSignals();

    old->deleteLater();
}

void TerminalView::onStateChanged(Transport::State state)
{
    updateStatusBar(state);

    if (state == Transport::State::Connected) {
        m_attempt = 0;
        m_gaveUp = false;
        hideOverlay();
    } else if (state == Transport::State::Disconnected || state == Transport::State::Error) {
        // The only way this view sees a Disconnected/Error transition
        // while still alive is the far end/link actually dying: there is
        // no "disconnect but keep the pane open" action, and closing the
        // pane deletes this object rather than leaving it around to
        // observe a deliberate disconnect.
        if (m_autoReconnect) {
            if (!m_gaveUp)
                scheduleReconnect();
        } else {
            // Reported directly: with auto-reconnect off for this pane,
            // nothing ever showed the overlay at all - scheduleReconnect()
            // is the only place that does, and it's never called here.
            // A disconnected pane must always have *some* visible way
            // back, regardless of this setting.
            showManualReconnectPrompt();
        }
    }
}

void TerminalView::scheduleReconnect()
{
    if (m_attempt >= ReconnectPolicy::kMaxAttempts) {
        m_gaveUp = true;
        ensureOverlay();
        m_overlayLabel->setText(QStringLiteral("Connection lost. Gave up after %1 attempts.").arg(ReconnectPolicy::kMaxAttempts));
        m_overlayButton->setText(QStringLiteral("Retry Now"));
        showOverlay();
        return;
    }

    ++m_attempt;
    m_secondsRemaining = ReconnectPolicy::delaySecondsForAttempt(m_attempt);

    ensureOverlay();
    m_overlayButton->setText(QStringLiteral("Cancel"));
    updateOverlayText();
    showOverlay();

    m_countdownTimer->start(1000);
    m_reconnectTimer->start(m_secondsRemaining * 1000);
}

void TerminalView::showManualReconnectPrompt()
{
    ensureOverlay();
    m_overlayLabel->setText(QStringLiteral("Disconnected."));
    m_overlayButton->setText(QStringLiteral("Reconnect Now"));
    showOverlay();
}

void TerminalView::tickCountdown()
{
    --m_secondsRemaining;
    if (m_secondsRemaining <= 0) {
        m_countdownTimer->stop();
        return;
    }
    updateOverlayText();
}

void TerminalView::updateOverlayText()
{
    m_overlayLabel->setText(QStringLiteral("Disconnected. Reconnecting in %1s… (attempt %2/%3)")
                                 .arg(m_secondsRemaining)
                                 .arg(m_attempt)
                                 .arg(ReconnectPolicy::kMaxAttempts));
}

void TerminalView::doReconnectNow()
{
    // Defense in depth alongside the status bar menu's own check: never
    // tear down a Transport that's still in the middle of connecting,
    // regardless of which of doReconnectNow()'s several callers (the
    // status bar menu, the overlay button, the automatic backoff timer)
    // triggered this - none of the others can currently reach here while
    // Connecting either, but this makes it structurally true rather than
    // relying on every current and future caller remembering to check.
    if (m_transport->state() == Transport::State::Connecting)
        return;

    m_countdownTimer->stop();
    m_reconnectTimer->stop();
    hideOverlay();

    // Reported directly: a manual "Retry Now" that itself fails again
    // could silently strand the pane with no overlay and no way back -
    // onStateChanged()'s Disconnected/Error branch only calls
    // scheduleReconnect() while !m_gaveUp, but nothing here ever cleared
    // m_gaveUp after a manual retry, so a second failure just skipped
    // straight past that branch with the overlay already hidden above.
    // Resetting here means a failed manual/automatic retry always
    // resumes the normal backoff-then-overlay flow instead of going
    // silent.
    m_gaveUp = false;
    m_attempt = 0;

    replaceTransport(m_recreate());
    connectToHost();
}

void TerminalView::cancelReconnect()
{
    m_countdownTimer->stop();
    m_reconnectTimer->stop();
    m_attempt = 0;
    m_gaveUp = false;
    hideOverlay();
}

void TerminalView::ensureOverlay()
{
    if (m_overlay)
        return;

    m_overlay = new QWidget(this);
    m_overlay->setStyleSheet(QStringLiteral("background-color: rgba(0, 0, 0, 160);"));

    m_overlayLabel = new QLabel(m_overlay);
    m_overlayLabel->setStyleSheet(QStringLiteral("color: white;"));
    m_overlayLabel->setAlignment(Qt::AlignCenter);

    m_overlayButton = new QPushButton(m_overlay);
    connect(m_overlayButton, &QPushButton::clicked, this, [this] {
        // Whether a countdown is actively running - not m_gaveUp - is
        // what actually distinguishes "Cancel" from "Reconnect Now"/
        // "Retry Now": m_gaveUp is false in the auto-reconnect-disabled
        // case too (showManualReconnectPrompt()), which also needs this
        // button to reconnect, not cancel a countdown that was never
        // started.
        if (m_countdownTimer->isActive() || m_reconnectTimer->isActive())
            cancelReconnect();
        else
            doReconnectNow();
    });

    auto *layout = new QVBoxLayout(m_overlay);
    layout->addStretch();
    layout->addWidget(m_overlayLabel);
    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    buttonRow->addWidget(m_overlayButton);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);
    layout->addStretch();

    m_overlay->setGeometry(rect());
    m_overlay->hide();
}

void TerminalView::showOverlay()
{
    m_overlay->setGeometry(rect());
    m_overlay->raise();
    m_overlay->show();
}

void TerminalView::hideOverlay()
{
    if (m_overlay)
        m_overlay->hide();
}

void TerminalView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_overlay)
        m_overlay->setGeometry(rect());
}

void TerminalView::ensureFindBar()
{
    if (m_findBar)
        return;

    m_findBar = new QWidget(this);
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(QStringLiteral("Find in scrollback (regex)..."));
    m_findStatusLabel = new QLabel(m_findBar);

    auto *prevButton = new QPushButton(QStringLiteral("Previous"), m_findBar);
    auto *nextButton = new QPushButton(QStringLiteral("Next"), m_findBar);
    auto *closeButton = new QPushButton(QStringLiteral("×"), m_findBar);
    closeButton->setFixedWidth(24);

    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { performSearch(false); });
    connect(prevButton, &QPushButton::clicked, this, [this] { performSearch(false); });
    connect(nextButton, &QPushButton::clicked, this, [this] { performSearch(true); });
    connect(closeButton, &QPushButton::clicked, this, &TerminalView::closeFindBar);
    connect(m_findEdit, &QLineEdit::textEdited, this, [this] { m_hasMatch = false; });

    auto *escapeShortcut = new QShortcut(Qt::Key_Escape, m_findBar);
    escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, &TerminalView::closeFindBar);

    auto *layout = new QHBoxLayout(m_findBar);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->addWidget(m_findEdit, 1);
    layout->addWidget(prevButton);
    layout->addWidget(nextButton);
    layout->addWidget(m_findStatusLabel);
    layout->addWidget(closeButton);

    // Insert above the terminal display, which is index 0 in our own
    // QVBoxLayout (see the constructor).
    qobject_cast<QVBoxLayout *>(this->layout())->insertWidget(0, m_findBar);
    m_findBar->hide();
}

void TerminalView::showFindBar()
{
    ensureFindBar();
    m_findBar->show();
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void TerminalView::closeFindBar()
{
    if (!m_findBar)
        return;

    m_findBar->hide();
    m_hasMatch = false;

    if (Konsole::ScreenWindow *window = m_display->screenWindow()) {
        window->clearSelection();
        window->setTrackOutput(true);
        window->scrollToEnd();
    }

    m_display->setFocus();
}

void TerminalView::performSearch(bool forwards)
{
    if (!m_findBar || m_findEdit->text().isEmpty())
        return;

    Konsole::ScreenWindow *window = m_display->screenWindow();
    if (!window)
        return;

    const QRegularExpression regex(m_findEdit->text(), QRegularExpression::CaseInsensitiveOption);
    if (!regex.isValid()) {
        m_findStatusLabel->setText(QStringLiteral("Invalid pattern"));
        return;
    }

    const int startColumn = m_hasMatch ? (forwards ? m_matchEndColumn : m_matchStartColumn) : 0;
    const int startLine = m_hasMatch ? (forwards ? m_matchEndLine : m_matchStartLine) : (forwards ? 0 : window->lineCount() - 1);

    auto *search = new HistorySearch(m_session->emulation(), regex, forwards, startColumn, startLine, this);
    connect(search, &HistorySearch::matchFound, this, [this, window](int sc, int sl, int ec, int el) {
        m_hasMatch = true;
        m_matchStartColumn = sc;
        m_matchStartLine = sl;
        m_matchEndColumn = ec;
        m_matchEndLine = el;

        window->setTrackOutput(false);
        window->scrollTo(sl);
        window->setSelectionStart(sc, sl, false);
        window->setSelectionEnd(ec, el);
        window->notifyOutputChanged();

        m_findStatusLabel->setText(QString());
    });
    connect(search, &HistorySearch::noMatchFound, this, [this] {
        m_hasMatch = false;
        m_findStatusLabel->setText(QStringLiteral("No matches"));
    });
    search->search();
    search->deleteLater();
}

void TerminalView::startLogging(const QString &path, bool includeTimestamps)
{
    stopLogging();

    auto *file = new QFile(path, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append)) {
        delete file;
        return;
    }
    m_logFile = file;
    m_logIncludeTimestamps = includeTimestamps;
    m_logAtLineStart = true; // a fresh log (or a fresh append session) starts a new line
}

void TerminalView::stopLogging()
{
    delete m_logFile;
    m_logFile = nullptr;
}

void TerminalView::onDataForLogging(const QByteArray &data)
{
    if (!m_logFile)
        return;

    if (!m_logIncludeTimestamps) {
        m_logFile->write(data);
        return;
    }

    // Raw transport bytes, not rendered lines - chunk boundaries don't
    // align to lines at all (a chunk can be a partial line, several
    // lines, or land mid-escape-sequence), so "per line" means: prepend
    // a timestamp right after every '\n', tracked across calls via
    // m_logAtLineStart since the next line's start can arrive in a
    // later, separate chunk.
    int start = 0;
    while (start < data.size()) {
        if (m_logAtLineStart) {
            m_logFile->write(QDateTime::currentDateTime().toString(QStringLiteral("[HH:mm:ss.zzz] ")).toUtf8());
            m_logAtLineStart = false;
        }
        const int newlineIndex = data.indexOf('\n', start);
        if (newlineIndex == -1) {
            m_logFile->write(data.mid(start));
            break;
        }
        m_logFile->write(data.mid(start, newlineIndex - start + 1));
        m_logAtLineStart = true;
        start = newlineIndex + 1;
    }
}

void TerminalView::updateStatusBar(Transport::State state)
{
    if (!m_sessionSnapshot) {
        // Loopback (or anything else that never gets a snapshot) has no
        // real endpoint to report and no real connection to lose - a
        // plain label, no state dot.
        m_statusIconLabel->clear();
        m_statusTextLabel->setText(QStringLiteral("loopback"));
        return;
    }

    QString stateText;
    switch (state) {
    case Transport::State::Connected:
        stateText = QStringLiteral("Connected");
        break;
    case Transport::State::Connecting:
        stateText = QStringLiteral("Connecting...");
        break;
    case Transport::State::Disconnected:
        stateText = QStringLiteral("Disconnected");
        break;
    case Transport::State::Error:
        stateText = QStringLiteral("Error");
        break;
    }

    QString text;
    switch (m_sessionSnapshot->type) {
    case ConnectionProfile::Type::Serial:
        // "attached"/"ERROR" per the port itself, matching how a serial
        // port is usually talked about (it's "attached", not "connected"),
        // and making a real problem impossible to mistake for the merely
        // idle Disconnected state.
        text = state == Transport::State::Error ? QStringLiteral("%1 - ERROR").arg(m_sessionSnapshot->serial.portName)
                                                 : QStringLiteral("%1 - %2").arg(m_sessionSnapshot->serial.portName, stateText);
        break;
    case ConnectionProfile::Type::Ssh:
        text = QStringLiteral("%1@%2:%3 - %4").arg(m_sessionSnapshot->ssh.username, m_sessionSnapshot->ssh.host).arg(m_sessionSnapshot->ssh.port).arg(stateText);
        break;
    case ConnectionProfile::Type::Telnet:
        text = QStringLiteral("%1:%2 - %3").arg(m_sessionSnapshot->telnetHost).arg(m_sessionSnapshot->telnetPort).arg(stateText);
        break;
    }

    m_statusIconLabel->setPixmap(StateIcon::icon(state).pixmap(10, 10));
    m_statusTextLabel->setText(text);
}
