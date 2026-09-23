#pragma once

#include <QWidget>

#include "transports/serial/SerialPortSettings.h"

class QComboBox;

// The COM-port-picker form used by both SerialConnectDialog (quick,
// unsaved connect) and ConnectionEditDialog (saved profiles) - a
// live-refreshable port combo plus the usual line settings.
class SerialSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SerialSettingsWidget(QWidget *parent = nullptr);

    SerialPortSettings settings() const;
    void setSettings(const SerialPortSettings &settings);

private:
    void refreshPorts();

    QComboBox *m_portCombo = nullptr;
    QComboBox *m_baudCombo = nullptr;
    QComboBox *m_dataBitsCombo = nullptr;
    QComboBox *m_parityCombo = nullptr;
    QComboBox *m_stopBitsCombo = nullptr;
    QComboBox *m_flowControlCombo = nullptr;
};
