#pragma once

#include <QSerialPort>

#include "SerialPortSettings.h"
#include "core/Transport.h"

// A COM port as a Transport. There is no remote "window size" concept for
// a serial line, so resize() is a no-op — unlike Telnet/SSH there is
// nothing to negotiate with the device on the other end.
class SerialTransport : public Transport
{
    Q_OBJECT

public:
    explicit SerialTransport(SerialPortSettings settings, QObject *parent = nullptr);

    void connectToHost() override;
    void disconnectFromHost() override;
    void resize(int cols, int rows) override;

public slots:
    void write(const QByteArray &data) override;

private slots:
    void onReadyRead();
    void onError(QSerialPort::SerialPortError error);

private:
    SerialPortSettings m_settings;
    QSerialPort *m_port = nullptr;
};
