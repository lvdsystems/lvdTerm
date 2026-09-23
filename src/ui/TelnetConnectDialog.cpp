#include "TelnetConnectDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

TelnetConnectDialog::TelnetConnectDialog(QWidget *parent)
    : QDialog(parent)
    , m_hostEdit(new QLineEdit(this))
    , m_portSpin(new QSpinBox(this))
    , m_rawModeCheck(new QCheckBox(QStringLiteral("Raw (no Telnet protocol negotiation)"), this))
{
    setWindowTitle(QStringLiteral("New Telnet Connection"));

    m_hostEdit->setPlaceholderText(QStringLiteral("hostname or IP address"));

    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(23);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("Host:"), m_hostEdit);
    form->addRow(QStringLiteral("Port:"), m_portSpin);
    form->addRow(QString(), m_rawModeCheck);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);

    m_hostEdit->setFocus();
}

QString TelnetConnectDialog::host() const
{
    return m_hostEdit->text().trimmed();
}

quint16 TelnetConnectDialog::port() const
{
    return static_cast<quint16>(m_portSpin->value());
}

bool TelnetConnectDialog::rawMode() const
{
    return m_rawModeCheck->isChecked();
}
