#include "ConnectionEditDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "SerialSettingsWidget.h"
#include "SshSettingsWidget.h"
#include "RdpSettingsWidget.h"
#include "VncSettingsWidget.h"
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
constexpr int kLocalIndex = 3;
constexpr int kVncIndex = 4;
constexpr int kRdpIndex = 5;
} // namespace

ConnectionEditDialog::ConnectionEditDialog(QWidget *parent, const QString &defaultFolder, const QStringList &existingFolders)
    : QDialog(parent)
    , m_id(QUuid::createUuid())
    , m_nameEdit(new QLineEdit(this))
    , m_folderCombo(new QComboBox(this))
    , m_typeCombo(new QComboBox(this))
    , m_autoReconnectCheck(new QCheckBox(QStringLiteral("Reconnect automatically if the connection drops"), this))
    , m_keyboardProfileCombo(new QComboBox(this))
    , m_viewerCombo(new QComboBox(this))
    , m_logSessionCheck(new QCheckBox(QStringLiteral("Log session to file"), this))
    , m_logFilePathEdit(new QLineEdit(this))
    , m_logFileBrowseButton(new QPushButton(QStringLiteral("Browse..."), this))
    , m_logTimestampCheck(new QCheckBox(QStringLiteral("Include timestamp for each line"), this))
    , m_typeStack(new QStackedWidget(this))
    , m_serialWidget(new SerialSettingsWidget(this))
    , m_telnetHostEdit(new QLineEdit(this))
    , m_telnetPortSpin(new QSpinBox(this))
    , m_telnetRawModeCheck(new QCheckBox(QStringLiteral("Raw (no Telnet protocol negotiation)"), this))
    , m_sshWidget(new SshSettingsWidget(this))
    , m_localShellEdit(new QLineEdit(this))
    , m_vncWidget(new VncSettingsWidget(this))
    , m_rdpWidget(new RdpSettingsWidget(this))
{
    setWindowTitle(QStringLiteral("New Connection"));

    m_nameEdit->setPlaceholderText(QStringLiteral("shown in the connection tree"));
    m_folderCombo->setEditable(true);
    m_folderCombo->setInsertPolicy(QComboBox::NoInsert); // typed text is read back in profile(), not added to the list
    m_folderCombo->addItems(existingFolders);
    m_folderCombo->setEditText(defaultFolder);
    m_folderCombo->lineEdit()->setPlaceholderText(QStringLiteral("pick one, or type a new one (e.g. Work/Servers) - blank for the root"));
    m_folderCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_folderCombo->setMinimumContentsLength(24);

    m_typeCombo->addItem(QStringLiteral("Serial"));
    m_typeCombo->addItem(QStringLiteral("Telnet"));
    m_typeCombo->addItem(QStringLiteral("SSH"));
    m_typeCombo->addItem(QStringLiteral("Local Shell"));
    m_typeCombo->addItem(QStringLiteral("VNC"));
    m_typeCombo->addItem(QStringLiteral("RDP"));
    m_typeCombo->setCurrentIndex(kSshIndex); // most new connections are SSH; setProfile() overrides this when editing an existing one
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, &ConnectionEditDialog::updateTypeStack);

    m_autoReconnectCheck->setChecked(true);

    m_keyboardProfileCombo->addItem(QStringLiteral("(Use Default)"), QString());
    for (const QString &name : KeyboardProfiles::names())
        m_keyboardProfileCombo->addItem(prettyProfileName(name), name);

    m_viewerCombo->addItem(QStringLiteral("Terminal"), static_cast<int>(ConnectionProfile::Viewer::Terminal));
    m_viewerCombo->addItem(QStringLiteral("Hex"), static_cast<int>(ConnectionProfile::Viewer::Hex));

    m_logFilePathEdit->setPlaceholderText(QStringLiteral("where to write the log file"));
    connect(m_logFileBrowseButton, &QPushButton::clicked, this, &ConnectionEditDialog::browseLogFilePath);
    // Timestamps only make sense while logging is actually enabled -
    // greyed out (not hidden) so its own checked state stays visible/
    // preserved even while logging is temporarily off.
    auto updateLogFieldsEnabled = [this] {
        const bool on = m_logSessionCheck->isChecked();
        m_logFilePathEdit->setEnabled(on);
        m_logFileBrowseButton->setEnabled(on);
        m_logTimestampCheck->setEnabled(on);
    };
    connect(m_logSessionCheck, &QCheckBox::toggled, this, updateLogFieldsEnabled);
    updateLogFieldsEnabled();

    auto *logFileRow = new QWidget(this);
    auto *logFileRowLayout = new QHBoxLayout(logFileRow);
    logFileRowLayout->setContentsMargins(0, 0, 0, 0);
    logFileRowLayout->addWidget(m_logFilePathEdit, 1);
    logFileRowLayout->addWidget(m_logFileBrowseButton);

    auto *telnetPage = new QWidget(this);
    auto *telnetForm = new QFormLayout(telnetPage);
    telnetForm->setContentsMargins(0, 0, 0, 0);
    m_telnetPortSpin->setRange(1, 65535);
    m_telnetPortSpin->setValue(23);
    telnetForm->addRow(QStringLiteral("Host:"), m_telnetHostEdit);
    telnetForm->addRow(QStringLiteral("Port:"), m_telnetPortSpin);
    telnetForm->addRow(QString(), m_telnetRawModeCheck);

    auto *localPage = new QWidget(this);
    auto *localForm = new QFormLayout(localPage);
    localForm->setContentsMargins(0, 0, 0, 0);
    m_localShellEdit->setPlaceholderText(QStringLiteral("blank = default shell ($SHELL / PowerShell)"));
    localForm->addRow(QStringLiteral("Shell:"), m_localShellEdit);

    m_typeStack->insertWidget(kSerialIndex, m_serialWidget);
    m_typeStack->insertWidget(kTelnetIndex, telnetPage);
    m_typeStack->insertWidget(kSshIndex, m_sshWidget);
    m_typeStack->insertWidget(kLocalIndex, localPage);
    m_typeStack->insertWidget(kVncIndex, m_vncWidget);
    m_typeStack->insertWidget(kRdpIndex, m_rdpWidget);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Name/Folder/Type, then immediately the type-specific fields
    // (host/port/username/authentication, via m_typeStack) - what a
    // connection actually connects *to* belongs right after choosing its
    // type, not after a run of secondary preferences (auto-reconnect,
    // keyboard, viewer, logging) that apply regardless of type.
    auto *topForm = new QFormLayout();
    topForm->addRow(QStringLiteral("Name:"), m_nameEdit);
    topForm->addRow(QStringLiteral("Folder:"), m_folderCombo);
    topForm->addRow(QStringLiteral("Type:"), m_typeCombo);

    auto *bottomForm = new QFormLayout();
    bottomForm->addRow(QString(), m_autoReconnectCheck);
    bottomForm->addRow(QStringLiteral("Keyboard:"), m_keyboardProfileCombo);
    bottomForm->addRow(QStringLiteral("Viewer:"), m_viewerCombo);
    bottomForm->addRow(QString(), m_logSessionCheck);
    bottomForm->addRow(QStringLiteral("Log file:"), logFileRow);
    bottomForm->addRow(QString(), m_logTimestampCheck);

#ifdef _WIN32
    const QString secretsNoteText = QStringLiteral(
        "Saved passwords/passphrases are encrypted with your Windows account (DPAPI). "
        "That protects the file if it's copied elsewhere, but not against other software "
        "running as you on this PC - local storage is never fully safe.");
#else
    const QString secretsNoteText = QStringLiteral(
        "Saved passwords/passphrases are encrypted with a key kept in your login keyring. "
        "That protects the file if it's copied elsewhere, but not against other software "
        "running as you on this PC - local storage is never fully safe.");
#endif
    auto *dpapiNote = new QLabel(secretsNoteText, this);
    dpapiNote->setWordWrap(true);
    dpapiNote->setStyleSheet(QStringLiteral("color: palette(mid);"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topForm);
    layout->addWidget(m_typeStack);
    layout->addLayout(bottomForm);
    layout->addWidget(dpapiNote);
    layout->addWidget(buttons);

    updateTypeStack();
    m_nameEdit->setFocus();
}

void ConnectionEditDialog::updateTypeStack()
{
    m_typeStack->setCurrentIndex(m_typeCombo->currentIndex());
}

void ConnectionEditDialog::browseLogFilePath()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Log Session To"), m_logFilePathEdit->text());
    if (!path.isEmpty())
        m_logFilePathEdit->setText(path);
}

ConnectionProfile::Type ConnectionEditDialog::indexToType(int index) const
{
    switch (index) {
    case kTelnetIndex:
        return ConnectionProfile::Type::Telnet;
    case kSshIndex:
        return ConnectionProfile::Type::Ssh;
    case kLocalIndex:
        return ConnectionProfile::Type::Local;
    case kVncIndex:
        return ConnectionProfile::Type::Vnc;
    case kRdpIndex:
        return ConnectionProfile::Type::Rdp;
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
    case ConnectionProfile::Type::Local:
        return kLocalIndex;
    case ConnectionProfile::Type::Vnc:
        return kVncIndex;
    case ConnectionProfile::Type::Rdp:
        return kRdpIndex;
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
    m_folderCombo->setEditText(profile.folder);
    m_autoReconnectCheck->setChecked(profile.autoReconnect);
    m_keyboardProfileCombo->setCurrentIndex(qMax(0, m_keyboardProfileCombo->findData(profile.keyboardProfile)));
    m_viewerCombo->setCurrentIndex(qMax(0, m_viewerCombo->findData(static_cast<int>(profile.viewer))));
    m_logSessionCheck->setChecked(profile.logSessionToFile);
    m_logFilePathEdit->setText(profile.logFilePath);
    m_logTimestampCheck->setChecked(profile.logIncludeTimestamps);

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
    case ConnectionProfile::Type::Local:
        m_localShellEdit->setText(profile.localShell);
        break;
    case ConnectionProfile::Type::Vnc:
        m_vncWidget->setSettings(profile.vnc);
        break;
    case ConnectionProfile::Type::Rdp:
        m_rdpWidget->setSettings(profile.rdp);
        break;
    }
}

ConnectionProfile ConnectionEditDialog::profile() const
{
    ConnectionProfile p;
    p.id = m_id;
    p.name = m_nameEdit->text().trimmed();
    // Normalised so "Work//Servers/ " and "/Work/Servers" are the same
    // folder as "Work/Servers" rather than a lookalike sibling.
    QStringList folderParts;
    for (const QString &part : m_folderCombo->currentText().split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (!part.trimmed().isEmpty())
            folderParts << part.trimmed();
    }
    p.folder = folderParts.join(QLatin1Char('/'));
    p.type = indexToType(m_typeCombo->currentIndex());
    p.autoReconnect = m_autoReconnectCheck->isChecked();
    p.keyboardProfile = m_keyboardProfileCombo->currentData().toString();
    p.viewer = static_cast<ConnectionProfile::Viewer>(m_viewerCombo->currentData().toInt());
    p.logSessionToFile = m_logSessionCheck->isChecked();
    p.logFilePath = m_logFilePathEdit->text().trimmed();
    p.logIncludeTimestamps = m_logTimestampCheck->isChecked();

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
    case ConnectionProfile::Type::Local:
        p.localShell = m_localShellEdit->text().trimmed();
        break;
    case ConnectionProfile::Type::Vnc:
        p.vnc = m_vncWidget->settings();
        break;
    case ConnectionProfile::Type::Rdp:
        p.rdp = m_rdpWidget->settings();
        break;
    }

    return p;
}
