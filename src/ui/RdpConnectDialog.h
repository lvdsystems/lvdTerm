#pragma once

#include <QDialog>

#include "transports/rdp/RdpConnectionSettings.h"

class RdpSettingsWidget;

// Modal "New RDP Connection" dialog (quick, unsaved connect).
class RdpConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RdpConnectDialog(QWidget *parent = nullptr);

    RdpConnectionSettings settings() const;
    void setSettings(const RdpConnectionSettings &settings);

private:
    RdpSettingsWidget *m_widget = nullptr;
};
