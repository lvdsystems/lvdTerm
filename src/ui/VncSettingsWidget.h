#pragma once

#include <QWidget>

#include "transports/vnc/VncConnectionSettings.h"

class QLineEdit;
class QSpinBox;

// Host/port/password form used by both VncConnectDialog (quick, unsaved
// connect) and ConnectionEditDialog (saved profiles).
class VncSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VncSettingsWidget(QWidget *parent = nullptr);

    VncConnectionSettings settings() const;
    void setSettings(const VncConnectionSettings &settings);

private:
    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
};
