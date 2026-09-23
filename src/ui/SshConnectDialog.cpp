#include "SshConnectDialog.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

#include "SshSettingsWidget.h"

SshConnectDialog::SshConnectDialog(QWidget *parent) : QDialog(parent), m_widget(new SshSettingsWidget(this))
{
    setWindowTitle(QStringLiteral("New SSH Connection"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_widget);
    layout->addWidget(buttons);
}

SshConnectionSettings SshConnectDialog::settings() const
{
    return m_widget->settings();
}
