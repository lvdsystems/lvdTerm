#pragma once

#include <QDialog>

#include "transports/vnc/VncConnectionSettings.h"

class VncSettingsWidget;

// Modal "New VNC Connection" dialog (quick, unsaved connect).
class VncConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VncConnectDialog(QWidget *parent = nullptr);

    VncConnectionSettings settings() const;
    void setSettings(const VncConnectionSettings &settings);

private:
    VncSettingsWidget *m_widget = nullptr;
};
