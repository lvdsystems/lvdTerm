#include "SshSettingsWidget.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>

SshSettingsWidget::SshSettingsWidget(QWidget *parent)
    : QWidget(parent)
    , m_hostEdit(new QLineEdit(this))
    , m_portSpin(new QSpinBox(this))
    , m_usernameEdit(new QLineEdit(this))
    , m_passwordRadio(new QRadioButton(QStringLiteral("Password"), this))
    , m_keyRadio(new QRadioButton(QStringLiteral("Private key"), this))
    , m_authStack(new QStackedWidget(this))
    , m_passwordEdit(new QLineEdit(this))
    , m_keyPathEdit(new QLineEdit(this))
    , m_passphraseEdit(new QLineEdit(this))
{
    m_hostEdit->setPlaceholderText(QStringLiteral("hostname or IP address"));
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(22);

    m_passwordRadio->setChecked(true);
    connect(m_passwordRadio, &QRadioButton::toggled, this, &SshSettingsWidget::updateAuthStack);

    auto *authMethodRow = new QWidget(this);
    auto *authMethodLayout = new QHBoxLayout(authMethodRow);
    authMethodLayout->setContentsMargins(0, 0, 0, 0);
    authMethodLayout->addWidget(m_passwordRadio);
    authMethodLayout->addWidget(m_keyRadio);
    authMethodLayout->addStretch();

    m_passwordEdit->setEchoMode(QLineEdit::Password);

    auto *passwordPage = new QWidget(this);
    auto *passwordForm = new QFormLayout(passwordPage);
    passwordForm->setContentsMargins(0, 0, 0, 0);
    passwordForm->addRow(QStringLiteral("Password:"), m_passwordEdit);

    m_keyPathEdit->setPlaceholderText(QStringLiteral("path to private key file"));
    auto *browseButton = new QPushButton(QStringLiteral("Browse..."), this);
    connect(browseButton, &QPushButton::clicked, this, &SshSettingsWidget::browseForKeyFile);

    auto *keyPathRow = new QWidget(this);
    auto *keyPathLayout = new QHBoxLayout(keyPathRow);
    keyPathLayout->setContentsMargins(0, 0, 0, 0);
    keyPathLayout->addWidget(m_keyPathEdit, 1);
    keyPathLayout->addWidget(browseButton);

    m_passphraseEdit->setEchoMode(QLineEdit::Password);

    auto *keyPage = new QWidget(this);
    auto *keyForm = new QFormLayout(keyPage);
    keyForm->setContentsMargins(0, 0, 0, 0);
    keyForm->addRow(QStringLiteral("Key file:"), keyPathRow);
    keyForm->addRow(QStringLiteral("Passphrase:"), m_passphraseEdit);

    m_authStack->addWidget(passwordPage);
    m_authStack->addWidget(keyPage);

    auto *form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(QStringLiteral("Host:"), m_hostEdit);
    form->addRow(QStringLiteral("Port:"), m_portSpin);
    form->addRow(QStringLiteral("Username:"), m_usernameEdit);
    form->addRow(QStringLiteral("Authenticate with:"), authMethodRow);
    form->addRow(m_authStack);

    updateAuthStack();
}

void SshSettingsWidget::updateAuthStack()
{
    m_authStack->setCurrentIndex(m_passwordRadio->isChecked() ? 0 : 1);
}

void SshSettingsWidget::browseForKeyFile()
{
    const QString startDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.ssh");
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Select Private Key"), startDir);
    if (!path.isEmpty())
        m_keyPathEdit->setText(path);
}

SshConnectionSettings SshSettingsWidget::settings() const
{
    SshConnectionSettings s;
    s.host = m_hostEdit->text().trimmed();
    s.port = static_cast<quint16>(m_portSpin->value());
    s.username = m_usernameEdit->text().trimmed();

    if (m_keyRadio->isChecked()) {
        s.authMethod = SshConnectionSettings::AuthMethod::PublicKey;
        s.privateKeyPath = m_keyPathEdit->text().trimmed();
        s.passphrase = m_passphraseEdit->text();
    } else {
        s.authMethod = SshConnectionSettings::AuthMethod::Password;
        s.password = m_passwordEdit->text();
    }

    return s;
}

void SshSettingsWidget::setSettings(const SshConnectionSettings &settings)
{
    m_hostEdit->setText(settings.host);
    m_portSpin->setValue(settings.port);
    m_usernameEdit->setText(settings.username);

    if (settings.authMethod == SshConnectionSettings::AuthMethod::PublicKey) {
        m_keyRadio->setChecked(true);
        m_keyPathEdit->setText(settings.privateKeyPath);
        m_passphraseEdit->setText(settings.passphrase);
    } else {
        m_passwordRadio->setChecked(true);
        m_passwordEdit->setText(settings.password);
    }
    updateAuthStack();
}
