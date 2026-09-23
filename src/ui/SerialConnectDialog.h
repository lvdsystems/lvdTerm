#pragma once

#include <QDialog>

#include "transports/serial/SerialPortSettings.h"

class SerialSettingsWidget;

// Modal "New Serial Connection" dialog for an immediate, unsaved
// connection. Just SerialSettingsWidget plus OK/Cancel - see that class
// for the actual form, which ConnectionEditDialog also uses for saved
// serial profiles.
class SerialConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SerialConnectDialog(QWidget *parent = nullptr);

    SerialPortSettings settings() const;

private:
    SerialSettingsWidget *m_widget = nullptr;
};
