#pragma once

#include <QWidget>

#include "transports/ssh/SshConnectionSettings.h"

class QCheckBox;
class QLineEdit;
class QSpinBox;
class QRadioButton;
class QStackedWidget;

// The host/port/username/auth form used by both SshConnectDialog
// (quick, unsaved connect) and ConnectionEditDialog (saved profiles).
class SshSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SshSettingsWidget(QWidget *parent = nullptr);

    SshConnectionSettings settings() const;
    void setSettings(const SshConnectionSettings &settings);

private:
    void browseForKeyFile();
    void updateAuthStack();

    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QLineEdit *m_usernameEdit = nullptr;
    QCheckBox *m_promptUsernameCheck = nullptr;

    QRadioButton *m_passwordRadio = nullptr;
    QRadioButton *m_keyRadio = nullptr;
    QStackedWidget *m_authStack = nullptr;

    QLineEdit *m_passwordEdit = nullptr;
    QCheckBox *m_promptPasswordCheck = nullptr;
    QLineEdit *m_keyPathEdit = nullptr;
    QLineEdit *m_passphraseEdit = nullptr;
};
