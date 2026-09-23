#include "TerminalSession.h"

#include "History.h"
#include "ScreenWindow.h"
#include "TerminalDisplay.h"
#include "core/Transport.h"

using Konsole::Emulation;
using Konsole::HistoryTypeBuffer;
using Konsole::TerminalDisplay;
using Konsole::Vt102Emulation;

TerminalSession::TerminalSession(QObject *parent)
    : QObject(parent)
    , m_emulation(new Vt102Emulation())
{
    m_emulation->setParent(this);
    m_emulation->setHistory(HistoryTypeBuffer(5000));
    m_emulation->setKeyBindings(QString()); // falls back to the built-in default translator

    connect(m_emulation, &Emulation::sendData, this, &TerminalSession::onEmulationSendData);
    connect(m_emulation, &Emulation::imageSizeChanged, this, [this](int, int) { updateTerminalSize(); });
    connect(m_emulation, &Emulation::titleChanged, this, &TerminalSession::titleChanged);
}

TerminalSession::~TerminalSession() = default;

void TerminalSession::setTransport(Transport *transport)
{
    if (m_transport == transport)
        return;

    if (m_transport)
        disconnect(m_transport, nullptr, this, nullptr);

    m_transport = transport;

    if (m_transport)
        connect(m_transport, &Transport::readyRead, this, &TerminalSession::onTransportReadyRead);
}

void TerminalSession::attachView(TerminalDisplay *view)
{
    m_view = view;

    // Emulation <-> view wiring, mirroring upstream Session::addView().
    connect(view, &TerminalDisplay::keyPressedSignal, m_emulation, &Emulation::sendKeyEvent);
    connect(view, SIGNAL(mouseSignal(int, int, int, int)), m_emulation, SLOT(sendMouseEvent(int, int, int, int)));
    connect(view, SIGNAL(sendStringToEmu(const char *)), m_emulation, SLOT(sendString(const char *)));

    connect(m_emulation, &Emulation::programUsesMouseChanged, view, &TerminalDisplay::setUsesMouse);
    view->setUsesMouse(m_emulation->programUsesMouse());

    connect(m_emulation, &Emulation::programBracketedPasteModeChanged, view, &TerminalDisplay::setBracketedPasteMode);
    view->setBracketedPasteMode(m_emulation->programBracketedPasteMode());

    connect(m_emulation, &Emulation::stateSet, this, [this](int state) {
        if (state == Konsole::NOTIFYBELL)
            emit bellRequest(QString());
    });

    view->setScreenWindow(m_emulation->createWindow());

    // Also part of upstream's Session::addView() wiring - without it,
    // TerminalDisplay::selectionChanged() (which re-emits copyAvailable(),
    // see TerminalView's auto-copy-on-select hookup) never fires, since
    // it's a plain public slot rather than something the display connects
    // to its own screen window internally.
    connect(view->screenWindow(), &Konsole::ScreenWindow::selectionChanged, view, &TerminalDisplay::selectionChanged);

    connect(view, SIGNAL(changedContentSizeSignal(int, int)), this, SLOT(onViewSizeChanged(int, int)));

    updateTerminalSize();
}

void TerminalSession::onTransportReadyRead(const QByteArray &data)
{
    m_emulation->receiveData(data.constData(), data.size());
}

void TerminalSession::onEmulationSendData(const char *data, int len)
{
    if (m_transport)
        m_transport->write(QByteArray(data, len));
}

void TerminalSession::onViewSizeChanged(int /*height*/, int /*width*/)
{
    updateTerminalSize();
}

void TerminalSession::updateTerminalSize()
{
    if (!m_view)
        return;

    const int lines = m_view->lines();
    const int columns = m_view->columns();

    if (lines < 1 || columns < 1)
        return;

    m_emulation->setImageSize(lines, columns);

    if (m_transport)
        m_transport->resize(columns, lines);
}
