#pragma once

#include <QDialog>

#include "transports/ssh/SshConnectionSettings.h"

class SshSettingsWidget;

// Modal "New SSH Connection" dialog for an immediate, unsaved
// connection. Just SshSettingsWidget plus OK/Cancel - see that class for
// the actual form, which ConnectionEditDialog also uses for saved SSH
// profiles.
class SshConnectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SshConnectDialog(QWidget *parent = nullptr);

    SshConnectionSettings settings() const;

private:
    SshSettingsWidget *m_widget = nullptr;
};
