#pragma once

#include <QWidget>

#include "transports/rdp/RdpConnectionSettings.h"

class QLineEdit;
class QSpinBox;

// Host/port/credentials/desktop-size form used by both RdpConnectDialog
// (quick, unsaved connect) and ConnectionEditDialog (saved profiles).
class RdpSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RdpSettingsWidget(QWidget *parent = nullptr);

    RdpConnectionSettings settings() const;
    void setSettings(const RdpConnectionSettings &settings);

private:
    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QLineEdit *m_domainEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QSpinBox *m_widthSpin = nullptr;
    QSpinBox *m_heightSpin = nullptr;
};
