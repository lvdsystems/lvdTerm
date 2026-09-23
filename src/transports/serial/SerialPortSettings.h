#pragma once

#include <QSerialPort>
#include <QString>

// Plain value type describing how to open a COM port. Kept separate from
// SerialTransport so the connect dialog (and, later, saved connection
// profiles) can build/store one without depending on QSerialPort directly
// beyond its enums.
struct SerialPortSettings
{
    QString portName;
    qint32 baudRate = QSerialPort::Baud115200;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
};
