#pragma once

#include <QDialog>

class QLineEdit;
class QSpinBox;
class QCheckBox;

// Modal "New Telnet Connection" dialog: host + port, and a "Raw" toggle
// (see RawTransport) for a plain TCP connection with no Telnet protocol
// negotiation at all - PuTTY's "Raw" connection type. ConnectionEditDialog
// reuses these same fields as the persisted profile payload for this
// connection type.
class TelnetConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TelnetConnectDialog(QWidget *parent = nullptr);

    QString host() const;
    quint16 port() const;
    bool rawMode() const;

private:
    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QCheckBox *m_rawModeCheck = nullptr;
};
