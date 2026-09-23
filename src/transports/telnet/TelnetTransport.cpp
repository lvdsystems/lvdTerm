#include "TelnetTransport.h"

TelnetTransport::TelnetTransport(QString host, quint16 port, QObject *parent)
    : Transport(parent)
    , m_host(std::move(host))
    , m_port(port)
    , m_socket(new QTcpSocket(this))
{
    connect(m_socket, &QTcpSocket::connected, this, &TelnetTransport::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &TelnetTransport::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TelnetTransport::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &TelnetTransport::onSocketError);
}

void TelnetTransport::connectToHost()
{
    setState(State::Connecting);
    m_parseState = ParseState::Data;
    m_subNegBuffer.clear();
    m_plainBuffer.clear();
    m_nawsEnabled = false;
    m_socket->connectToHost(m_host, m_port);
}

void TelnetTransport::disconnectFromHost()
{
    m_socket->disconnectFromHost();
}

void TelnetTransport::resize(int cols, int rows)
{
    m_cols = cols;
    m_rows = rows;
    sendWindowSize();
}

void TelnetTransport::write(const QByteArray &data)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    // A literal 0xFF in user data must be doubled, or the remote would
    // misparse it as the start of a telnet command (RFC 854).
    QByteArray escaped;
    escaped.reserve(data.size());
    for (char c : data) {
        escaped.append(c);
        if (static_cast<quint8>(c) == IAC)
            escaped.append(char(IAC));
    }
    m_socket->write(escaped);
}

void TelnetTransport::onConnected()
{
    setState(State::Connected);
}

void TelnetTransport::onDisconnected()
{
    setState(State::Disconnected);
}

void TelnetTransport::onReadyRead()
{
    processIncoming(m_socket->readAll());
}

void TelnetTransport::onSocketError(QAbstractSocket::SocketError /*error*/)
{
    emit errorOccurred(m_socket->errorString());
    setState(State::Error);
}

void TelnetTransport::processIncoming(const QByteArray &chunk)
{
    for (unsigned char b : chunk) {
        switch (m_parseState) {
        case ParseState::Data:
            if (b == IAC)
                m_parseState = ParseState::IacSeen;
            else
                m_plainBuffer.append(static_cast<char>(b));
            break;

        case ParseState::IacSeen:
            switch (b) {
            case IAC: // escaped 0xFF: a literal data byte, not a command
                m_plainBuffer.append(static_cast<char>(IAC));
                m_parseState = ParseState::Data;
                break;
            case WILL:
            case WONT:
            case DO:
            case DONT:
                m_pendingCommand = b;
                m_parseState = ParseState::Negotiate;
                break;
            case SB:
                m_subNegBuffer.clear();
                m_parseState = ParseState::SubNegData;
                break;
            default:
                // NOP, GA, DM, IP, etc. - nothing we act on.
                m_parseState = ParseState::Data;
                break;
            }
            break;

        case ParseState::Negotiate:
            handleCommand(m_pendingCommand, b);
            m_parseState = ParseState::Data;
            break;

        case ParseState::SubNegData:
            if (b == IAC)
                m_parseState = ParseState::SubNegIacSeen;
            else
                m_subNegBuffer.append(static_cast<char>(b));
            break;

        case ParseState::SubNegIacSeen:
            if (b == SE) {
                handleSubnegotiation(m_subNegBuffer);
                m_parseState = ParseState::Data;
            } else if (b == IAC) {
                m_subNegBuffer.append(static_cast<char>(IAC));
                m_parseState = ParseState::SubNegData;
            } else {
                // Malformed subnegotiation; drop back to a known state
                // rather than getting stuck.
                m_parseState = ParseState::Data;
            }
            break;
        }
    }

    if (!m_plainBuffer.isEmpty()) {
        emit readyRead(m_plainBuffer);
        m_plainBuffer.clear();
    }
}

void TelnetTransport::handleCommand(quint8 command, quint8 option)
{
    switch (command) {
    case WILL:
        // The remote end is offering to enable an option it controls.
        // ECHO/SGA: agree - we want the server doing remote echo and
        // suppressing go-ahead, which matches how our terminal core
        // already behaves (it never echoes locally; see TerminalSession).
        if (option == OPT_ECHO || option == OPT_SGA)
            sendCommand(DO, option);
        else
            sendCommand(DONT, option);
        break;

    case DO:
        // The remote end is asking us to enable an option we control.
        if (option == OPT_NAWS) {
            sendCommand(WILL, option);
            m_nawsEnabled = true;
            sendWindowSize();
        } else if (option == OPT_TTYPE) {
            sendCommand(WILL, option);
        } else {
            sendCommand(WONT, option);
        }
        break;

    case WONT:
        if (option == OPT_NAWS)
            m_nawsEnabled = false;
        break;

    case DONT:
        break;
    }
}

void TelnetTransport::handleSubnegotiation(const QByteArray &payload)
{
    if (payload.isEmpty())
        return;

    const auto option = static_cast<quint8>(payload.at(0));

    // TERMINAL-TYPE: SEND(1) asks us to report a type; reply with IS(0).
    if (option == OPT_TTYPE && payload.size() >= 2 && static_cast<quint8>(payload.at(1)) == 1) {
        QByteArray body;
        body.append(static_cast<char>(0)); // IS
        body.append("xterm-256color");
        sendSubnegotiation(OPT_TTYPE, body);
    }
}

void TelnetTransport::sendCommand(quint8 command, quint8 option)
{
    const char out[3] = {static_cast<char>(IAC), static_cast<char>(command), static_cast<char>(option)};
    m_socket->write(out, sizeof(out));
}

void TelnetTransport::sendSubnegotiation(quint8 option, const QByteArray &payload)
{
    QByteArray out;
    out.append(static_cast<char>(IAC));
    out.append(static_cast<char>(SB));
    out.append(static_cast<char>(option));
    for (char c : payload) {
        out.append(c);
        if (static_cast<quint8>(c) == IAC)
            out.append(static_cast<char>(IAC)); // escape a literal 0xFF in the payload
    }
    out.append(static_cast<char>(IAC));
    out.append(static_cast<char>(SE));
    m_socket->write(out);
}

void TelnetTransport::sendWindowSize()
{
    if (!m_nawsEnabled)
        return;

    QByteArray body;
    body.append(static_cast<char>((m_cols >> 8) & 0xFF));
    body.append(static_cast<char>(m_cols & 0xFF));
    body.append(static_cast<char>((m_rows >> 8) & 0xFF));
    body.append(static_cast<char>(m_rows & 0xFF));
    sendSubnegotiation(OPT_NAWS, body);
}
