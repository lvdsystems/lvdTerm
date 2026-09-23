#include "SerialSettingsWidget.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSerialPortInfo>

SerialSettingsWidget::SerialSettingsWidget(QWidget *parent)
    : QWidget(parent)
    , m_portCombo(new QComboBox(this))
    , m_baudCombo(new QComboBox(this))
    , m_dataBitsCombo(new QComboBox(this))
    , m_parityCombo(new QComboBox(this))
    , m_stopBitsCombo(new QComboBox(this))
    , m_flowControlCombo(new QComboBox(this))
{
    m_portCombo->setEditable(true); // allow a port name the scan didn't find

    auto *refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
    connect(refreshButton, &QPushButton::clicked, this, &SerialSettingsWidget::refreshPorts);

    auto *portFieldRow = new QWidget(this);
    auto *portFieldLayout = new QHBoxLayout(portFieldRow);
    portFieldLayout->setContentsMargins(0, 0, 0, 0);
    portFieldLayout->addWidget(m_portCombo, 1);
    portFieldLayout->addWidget(refreshButton);

    m_baudCombo->addItems({QStringLiteral("300"), QStringLiteral("1200"), QStringLiteral("2400"),
                            QStringLiteral("4800"), QStringLiteral("9600"), QStringLiteral("19200"),
                            QStringLiteral("38400"), QStringLiteral("57600"), QStringLiteral("115200"),
                            QStringLiteral("230400")});
    m_baudCombo->setCurrentText(QStringLiteral("115200"));
    m_baudCombo->setEditable(true); // non-standard rates are common on embedded gear

    m_dataBitsCombo->addItem(QStringLiteral("5"), QSerialPort::Data5);
    m_dataBitsCombo->addItem(QStringLiteral("6"), QSerialPort::Data6);
    m_dataBitsCombo->addItem(QStringLiteral("7"), QSerialPort::Data7);
    m_dataBitsCombo->addItem(QStringLiteral("8"), QSerialPort::Data8);
    m_dataBitsCombo->setCurrentIndex(m_dataBitsCombo->findData(QSerialPort::Data8));

    m_parityCombo->addItem(QStringLiteral("None"), QSerialPort::NoParity);
    m_parityCombo->addItem(QStringLiteral("Even"), QSerialPort::EvenParity);
    m_parityCombo->addItem(QStringLiteral("Odd"), QSerialPort::OddParity);
    m_parityCombo->addItem(QStringLiteral("Space"), QSerialPort::SpaceParity);
    m_parityCombo->addItem(QStringLiteral("Mark"), QSerialPort::MarkParity);

    m_stopBitsCombo->addItem(QStringLiteral("1"), QSerialPort::OneStop);
    m_stopBitsCombo->addItem(QStringLiteral("1.5"), QSerialPort::OneAndHalfStop);
    m_stopBitsCombo->addItem(QStringLiteral("2"), QSerialPort::TwoStop);

    m_flowControlCombo->addItem(QStringLiteral("None"), QSerialPort::NoFlowControl);
    m_flowControlCombo->addItem(QStringLiteral("RTS/CTS"), QSerialPort::HardwareControl);
    m_flowControlCombo->addItem(QStringLiteral("XON/XOFF"), QSerialPort::SoftwareControl);

    auto *form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(QStringLiteral("Port:"), portFieldRow);
    form->addRow(QStringLiteral("Baud rate:"), m_baudCombo);
    form->addRow(QStringLiteral("Data bits:"), m_dataBitsCombo);
    form->addRow(QStringLiteral("Parity:"), m_parityCombo);
    form->addRow(QStringLiteral("Stop bits:"), m_stopBitsCombo);
    form->addRow(QStringLiteral("Flow control:"), m_flowControlCombo);

    refreshPorts();
}

void SerialSettingsWidget::refreshPorts()
{
    const QString previous = m_portCombo->currentText();

    m_portCombo->clear();
    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts()) {
        const QString label = info.description().isEmpty()
            ? info.portName()
            : QStringLiteral("%1 (%2)").arg(info.portName(), info.description());
        m_portCombo->addItem(label, info.portName());
    }

    if (!previous.isEmpty()) {
        const int index = m_portCombo->findData(previous);
        if (index >= 0)
            m_portCombo->setCurrentIndex(index);
        else
            m_portCombo->setCurrentText(previous);
    }
}

SerialPortSettings SerialSettingsWidget::settings() const
{
    SerialPortSettings s;

    // Items added by refreshPorts() carry the bare port name as their data;
    // a name typed into the editable combo that doesn't match any of them
    // has no associated data, so fall back to the literal text.
    s.portName = m_portCombo->currentData().toString();
    if (s.portName.isEmpty())
        s.portName = m_portCombo->currentText();

    s.baudRate = m_baudCombo->currentText().toInt();
    s.dataBits = static_cast<QSerialPort::DataBits>(m_dataBitsCombo->currentData().toInt());
    s.parity = static_cast<QSerialPort::Parity>(m_parityCombo->currentData().toInt());
    s.stopBits = static_cast<QSerialPort::StopBits>(m_stopBitsCombo->currentData().toInt());
    s.flowControl = static_cast<QSerialPort::FlowControl>(m_flowControlCombo->currentData().toInt());

    return s;
}

void SerialSettingsWidget::setSettings(const SerialPortSettings &settings)
{
    if (!settings.portName.isEmpty()) {
        const int index = m_portCombo->findData(settings.portName);
        if (index >= 0)
            m_portCombo->setCurrentIndex(index);
        else
            m_portCombo->setCurrentText(settings.portName);
    }

    m_baudCombo->setCurrentText(QString::number(settings.baudRate));
    m_dataBitsCombo->setCurrentIndex(m_dataBitsCombo->findData(settings.dataBits));
    m_parityCombo->setCurrentIndex(m_parityCombo->findData(settings.parity));
    m_stopBitsCombo->setCurrentIndex(m_stopBitsCombo->findData(settings.stopBits));
    m_flowControlCombo->setCurrentIndex(m_flowControlCombo->findData(settings.flowControl));
}
