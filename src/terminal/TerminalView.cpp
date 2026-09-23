#include "TerminalView.h"

#include <QCoreApplication>
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
    , m_reconnectTimer(new QTimer(this))
    , m_countdownTimer(new QTimer(this))
{
    m_transport->setParent(this);

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
    layout->addWidget(statusBar); // stretch 0 (default): fixed to its sizeHint, above the display
    layout->addWidget(m_display, 1); // stretch 1: take all space not needed by the status bar above
    updateStatusBar(m_transport->state());

    applyAppearance();
    m_display->setScrollBarPosition(QTermWidget::ScrollBarRight);
    m_display->setKeyboardCursorShape(QTermWidget::KeyboardCursorShape::BlockCursor);
    m_display->setBlinkingCursor(true);

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

    // Right-click context menu: configureRequest() is TerminalDisplay's own
    // "user right-clicked (and no mouse-aware program has the mouse right
    // now, or Shift is held)" signal, meant for exactly this.
    connect(m_display, &TerminalDisplay::configureRequest, this, [this](const QPoint &position) {
        QMenu menu(m_display);
        QAction *clearBufferAction = menu.addAction(QStringLiteral("Clear Buffer"));
        connect(clearBufferAction, &QAction::triggered, this, &TerminalView::clearBuffer);
        QAction *clearScreenAction = menu.addAction(QStringLiteral("Clear Screen"));
        connect(clearScreenAction, &QAction::triggered, this, &TerminalView::clearScreen);
        menu.addSeparator();
        QAction *copyAction = menu.addAction(QStringLiteral("Copy"));
        copyAction->setEnabled(!m_display->screenWindow()->selectedText(false).isEmpty());
        connect(copyAction, &QAction::triggered, this, &TerminalView::copySelection);
        QAction *pasteAction = menu.addAction(QStringLiteral("Paste"));
        connect(pasteAction, &QAction::triggered, this, &TerminalView::pasteFromClipboard);
        menu.exec(m_display->mapToGlobal(position));
    });

    m_session->setTransport(m_transport);
    // See KeyboardProfiles.h/ConnectionProfile::keyboardProfile: an
    // explicit per-connection choice wins; otherwise fall back to the
    // global default (itself "default" unless changed in Settings).
    const QString keyboardProfile =
        (m_sessionSnapshot && !m_sessionSnapshot->keyboardProfile.isEmpty()) ? m_sessionSnapshot->keyboardProfile : AppSettings::instance().defaultKeyboardProfile();
    m_session->setKeyboardProfile(keyboardProfile);
    m_session->attachView(m_display);

    setFocusProxy(m_display);

    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &TerminalView::doReconnectNow);
    connect(m_countdownTimer, &QTimer::timeout, this, &TerminalView::tickCountdown);

    // Live-apply Settings dialog changes to every already-open pane.
    connect(&AppSettings::instance(), &AppSettings::terminalAppearanceChanged, this, &TerminalView::applyAppearance);

    auto *findShortcut = new QShortcut(QKeySequence::Find, this);
    findShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut, &QShortcut::activated, this, &TerminalView::showFindBar);

    hookTransportSignals();
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
    // resize() updates real widget geometry (unlike a synthetic
    // QResizeEvent alone, which would just re-derive columns/lines from
    // whatever geometry the display already has - insufficient here since
    // a hidden tab page's geometry can genuinely be stale/wrong, not just
    // un-notified). TerminalDisplay::resizeEvent() ignores its event
    // argument entirely and just recomputes columns/lines from the
    // widget's actual current geometry, so resize() alone triggers the fix
    // whenever the size actually changes; the explicit event covers the
    // (harmless either way) case where it happens to already match.
    resize(size);
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
}

void TerminalView::replaceTransport(Transport *newTransport)
{
    Transport *old = m_transport;
    disconnect(old, nullptr, this, nullptr);

    m_transport = newTransport;
    m_transport->setParent(this);
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
    } else if ((state == Transport::State::Disconnected || state == Transport::State::Error) && m_autoReconnect && !m_gaveUp) {
        // The only way this view sees a Disconnected/Error transition
        // while still alive is the far end/link actually dying: there is
        // no "disconnect but keep the pane open" action, and closing the
        // pane deletes this object rather than leaving it around to
        // observe a deliberate disconnect.
        scheduleReconnect();
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
    m_countdownTimer->stop();
    m_reconnectTimer->stop();
    hideOverlay();

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
        if (m_gaveUp)
            doReconnectNow();
        else
            cancelReconnect();
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

void TerminalView::startLogging(const QString &path)
{
    stopLogging();

    auto *file = new QFile(path, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append)) {
        delete file;
        return;
    }
    m_logFile = file;
}

void TerminalView::stopLogging()
{
    delete m_logFile;
    m_logFile = nullptr;
}

void TerminalView::onDataForLogging(const QByteArray &data)
{
    if (m_logFile)
        m_logFile->write(data);
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
