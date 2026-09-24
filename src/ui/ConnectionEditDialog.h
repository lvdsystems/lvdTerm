#pragma once

#include <QDialog>

#include "core/ConnectionProfile.h"

class QLineEdit;
class QSpinBox;
class QComboBox;
class QStackedWidget;
class QLabel;
class QCheckBox;
class QPushButton;
class SerialSettingsWidget;
class SshSettingsWidget;

// "New/Edit Connection" dialog for the saved-connections tree: a name,
// a folder path, a type, and that type's settings (via
// SerialSettingsWidget/SshSettingsWidget, shared with the quick-connect
// dialogs; telnet's host+port is simple enough to inline here).
class ConnectionEditDialog : public QDialog
{
    Q_OBJECT

public:
    // defaultFolder pre-fills the folder field, e.g. when adding a new
    // connection from inside a folder already selected in the tree.
    explicit ConnectionEditDialog(QWidget *parent = nullptr, const QString &defaultFolder = QString());

    void setProfile(const ConnectionProfile &profile); // switches to edit mode
    ConnectionProfile profile() const;                 // preserves id when editing

private:
    void updateTypeStack();
    void browseLogFilePath();

    ConnectionProfile::Type indexToType(int index) const;
    int typeToIndex(ConnectionProfile::Type type) const;

    QUuid m_id;

    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_folderEdit = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QCheckBox *m_autoReconnectCheck = nullptr;
    QComboBox *m_keyboardProfileCombo = nullptr; // see KeyboardProfiles.h - applies regardless of type
    QComboBox *m_viewerCombo = nullptr; // Terminal or Hex - see ConnectionProfile::Viewer
    QCheckBox *m_logSessionCheck = nullptr;
    QLineEdit *m_logFilePathEdit = nullptr;
    QPushButton *m_logFileBrowseButton = nullptr;
    // Only ever offered here, per saved connection - not a global
    // AppSettings default, and not on the ad hoc quick-log action.
    QCheckBox *m_logTimestampCheck = nullptr;
    QStackedWidget *m_typeStack = nullptr;

    SerialSettingsWidget *m_serialWidget = nullptr;
    QLineEdit *m_telnetHostEdit = nullptr;
    QSpinBox *m_telnetPortSpin = nullptr;
    QCheckBox *m_telnetRawModeCheck = nullptr;
    SshSettingsWidget *m_sshWidget = nullptr;
};
