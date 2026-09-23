#include "SerialTransport.h"

SerialTransport::SerialTransport(SerialPortSettings settings, QObject *parent)
    : Transport(parent)
    , m_settings(std::move(settings))
    , m_port(new QSerialPort(this))
{
    connect(m_port, &QSerialPort::readyRead, this, &SerialTransport::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred, this, &SerialTransport::onError);
}

void SerialTransport::connectToHost()
{
    if (m_port->isOpen())
        return;

    setState(State::Connecting);

    m_port->setPortName(m_settings.portName);
    m_port->setBaudRate(m_settings.baudRate);
    m_port->setDataBits(m_settings.dataBits);
    m_port->setParity(m_settings.parity);
    m_port->setStopBits(m_settings.stopBits);
    m_port->setFlowControl(m_settings.flowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        emit errorOccurred(m_port->errorString());
        setState(State::Error);
        return;
    }

    setState(State::Connected);
}

void SerialTransport::disconnectFromHost()
{
    if (m_port->isOpen())
        m_port->close();
    setState(State::Disconnected);
}

void SerialTransport::resize(int /*cols*/, int /*rows*/)
{
    // No window-size concept on a serial line.
}

void SerialTransport::write(const QByteArray &data)
{
    if (m_port->isOpen())
        m_port->write(data);
}

void SerialTransport::onReadyRead()
{
    emit readyRead(m_port->readAll());
}

void SerialTransport::onError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;

    emit errorOccurred(m_port->errorString());

    // ResourceError typically means the device went away (unplugged, other
    // end of a virtual pair closed, etc.) — treat that as a disconnect
    // rather than leaving the transport stuck in a broken "connected" state.
    if (error == QSerialPort::ResourceError) {
        m_port->close();
        setState(State::Disconnected);
    } else {
        setState(State::Error);
    }
}
