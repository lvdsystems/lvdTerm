#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

// Common interface between a TerminalSession (terminal emulation glue) and
// whatever byte stream feeds it: SSH, Telnet, a serial port, or (for
// bring-up/testing) a simple loopback. The terminal core never knows which
// one it is talking to.
//
// This is what replaces QTermWidget's Pty in the vendored core — see
// third_party/qtermwidget/VENDORING.md.
class Transport : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Disconnected,
        Connecting,
        Connected,
        Error
    };
    Q_ENUM(State)

    explicit Transport(QObject *parent = nullptr) : QObject(parent) {}
    ~Transport() override = default;

    virtual void connectToHost() = 0;
    virtual void disconnectFromHost() = 0;

    // Informs the remote end (or the physical port) that the terminal
    // viewport is now cols x rows. Transports that have no concept of a
    // window size (e.g. a bare loopback) may implement this as a no-op.
    virtual void resize(int cols, int rows) = 0;

    State state() const { return m_state; }

public slots:
    virtual void write(const QByteArray &data) = 0;

signals:
    void readyRead(const QByteArray &data);
    void stateChanged(Transport::State state);
    void errorOccurred(const QString &message);

protected:
    void setState(State state)
    {
        if (m_state != state) {
            m_state = state;
            emit stateChanged(m_state);
        }
    }

private:
    State m_state = State::Disconnected;
};
