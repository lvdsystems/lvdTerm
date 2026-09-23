#include "ConnectionEditDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "SerialSettingsWidget.h"
#include "SshSettingsWidget.h"
#include "terminal/KeyboardProfiles.h"

namespace
{
// KeyboardProfiles::names() returns lookup keys ("default", "linux",
// "vt100") - capitalize just for display, item data still carries the raw
// name ConnectionProfile::keyboardProfile/setKeyBindings() expect.
QString prettyProfileName(const QString &name)
{
    QString pretty = name;
    if (!pretty.isEmpty())
        pretty[0] = pretty[0].toUpper();
    return pretty;
}
} // namespace

namespace
{
// Index order in m_typeCombo/m_typeStack.
constexpr int kSerialIndex = 0;
constexpr int kTelnetIndex = 1;
constexpr int kSshIndex = 2;
} // namespace

ConnectionEditDialog::ConnectionEditDialog(QWidget *parent, const QString &defaultFolder)
    : QDialog(parent)
    , m_id(QUuid::createUuid())
    , m_nameEdit(new QLineEdit(this))
    , m_folderEdit(new QLineEdit(this))
    , m_typeCombo(new QComboBox(this))
    , m_autoReconnectCheck(new QCheckBox(QStringLiteral("Reconnect automatically if the connection drops"), this))
    , m_keyboardProfileCombo(new QComboBox(this))
    , m_typeStack(new QStackedWidget(this))
    , m_serialWidget(new SerialSettingsWidget(this))
    , m_telnetHostEdit(new QLineEdit(this))
    , m_telnetPortSpin(new QSpinBox(this))
    , m_telnetRawModeCheck(new QCheckBox(QStringLiteral("Raw (no Telnet protocol negotiation)"), this))
    , m_sshWidget(new SshSettingsWidget(this))
{
    setWindowTitle(QStringLiteral("New Connection"));

    m_nameEdit->setPlaceholderText(QStringLiteral("shown in the connection tree"));
    m_folderEdit->setText(defaultFolder);
    m_folderEdit->setPlaceholderText(QStringLiteral("e.g. Work/Servers - blank for the root"));

    m_typeCombo->addItem(QStringLiteral("Serial"));
    m_typeCombo->addItem(QStringLiteral("Telnet"));
    m_typeCombo->addItem(QStringLiteral("SSH"));
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, &ConnectionEditDialog::updateTypeStack);

    m_autoReconnectCheck->setChecked(true);

    m_keyboardProfileCombo->addItem(QStringLiteral("(Use Default)"), QString());
    for (const QString &name : KeyboardProfiles::names())
        m_keyboardProfileCombo->addItem(prettyProfileName(name), name);

    auto *telnetPage = new QWidget(this);
    auto *telnetForm = new QFormLayout(telnetPage);
    telnetForm->setContentsMargins(0, 0, 0, 0);
    m_telnetPortSpin->setRange(1, 65535);
    m_telnetPortSpin->setValue(23);
    telnetForm->addRow(QStringLiteral("Host:"), m_telnetHostEdit);
    telnetForm->addRow(QStringLiteral("Port:"), m_telnetPortSpin);
    telnetForm->addRow(QString(), m_telnetRawModeCheck);

    m_typeStack->insertWidget(kSerialIndex, m_serialWidget);
    m_typeStack->insertWidget(kTelnetIndex, telnetPage);
    m_typeStack->insertWidget(kSshIndex, m_sshWidget);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("Name:"), m_nameEdit);
    form->addRow(QStringLiteral("Folder:"), m_folderEdit);
    form->addRow(QStringLiteral("Type:"), m_typeCombo);
    form->addRow(QString(), m_autoReconnectCheck);
    form->addRow(QStringLiteral("Keyboard:"), m_keyboardProfileCombo);

    auto *dpapiNote = new QLabel(
        QStringLiteral("Saved passwords/passphrases are encrypted with your Windows account (DPAPI). "
                        "That protects the file if it's copied elsewhere, but not against other software "
                        "running as you on this PC - local storage is never fully safe."),
        this);
    dpapiNote->setWordWrap(true);
    dpapiNote->setStyleSheet(QStringLiteral("color: palette(mid);"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_typeStack);
    layout->addWidget(dpapiNote);
    layout->addWidget(buttons);

    updateTypeStack();
    m_nameEdit->setFocus();
}

void ConnectionEditDialog::updateTypeStack()
{
    m_typeStack->setCurrentIndex(m_typeCombo->currentIndex());
}

ConnectionProfile::Type ConnectionEditDialog::indexToType(int index) const
{
    switch (index) {
    case kTelnetIndex:
        return ConnectionProfile::Type::Telnet;
    case kSshIndex:
        return ConnectionProfile::Type::Ssh;
    default:
        return ConnectionProfile::Type::Serial;
    }
}

int ConnectionEditDialog::typeToIndex(ConnectionProfile::Type type) const
{
    switch (type) {
    case ConnectionProfile::Type::Telnet:
        return kTelnetIndex;
    case ConnectionProfile::Type::Ssh:
        return kSshIndex;
    case ConnectionProfile::Type::Serial:
        return kSerialIndex;
    }
    return kSerialIndex;
}

void ConnectionEditDialog::setProfile(const ConnectionProfile &profile)
{
    setWindowTitle(QStringLiteral("Edit Connection"));

    m_id = profile.id;
    m_nameEdit->setText(profile.name);
    m_folderEdit->setText(profile.folder);
    m_autoReconnectCheck->setChecked(profile.autoReconnect);
    m_keyboardProfileCombo->setCurrentIndex(qMax(0, m_keyboardProfileCombo->findData(profile.keyboardProfile)));

    m_typeCombo->setCurrentIndex(typeToIndex(profile.type));
    updateTypeStack();

    switch (profile.type) {
    case ConnectionProfile::Type::Serial:
        m_serialWidget->setSettings(profile.serial);
        break;
    case ConnectionProfile::Type::Telnet:
        m_telnetHostEdit->setText(profile.telnetHost);
        m_telnetPortSpin->setValue(profile.telnetPort);
        m_telnetRawModeCheck->setChecked(profile.rawMode);
        break;
    case ConnectionProfile::Type::Ssh:
        m_sshWidget->setSettings(profile.ssh);
        break;
    }
}

ConnectionProfile ConnectionEditDialog::profile() const
{
    ConnectionProfile p;
    p.id = m_id;
    p.name = m_nameEdit->text().trimmed();
    p.folder = m_folderEdit->text().trimmed();
    p.type = indexToType(m_typeCombo->currentIndex());
    p.autoReconnect = m_autoReconnectCheck->isChecked();
    p.keyboardProfile = m_keyboardProfileCombo->currentData().toString();

    switch (p.type) {
    case ConnectionProfile::Type::Serial:
        p.serial = m_serialWidget->settings();
        break;
    case ConnectionProfile::Type::Telnet:
        p.telnetHost = m_telnetHostEdit->text().trimmed();
        p.telnetPort = static_cast<quint16>(m_telnetPortSpin->value());
        p.rawMode = m_telnetRawModeCheck->isChecked();
        break;
    case ConnectionProfile::Type::Ssh:
        p.ssh = m_sshWidget->settings();
        break;
    }

    return p;
}
