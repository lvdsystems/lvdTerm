#include "RdpConnectDialog.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

#include "RdpSettingsWidget.h"

RdpConnectDialog::RdpConnectDialog(QWidget *parent) : QDialog(parent), m_widget(new RdpSettingsWidget(this))
{
    setWindowTitle(QStringLiteral("New RDP Connection"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_widget);
    layout->addWidget(buttons);
}

void RdpConnectDialog::setSettings(const RdpConnectionSettings &settings)
{
    m_widget->setSettings(settings);
}

RdpConnectionSettings RdpConnectDialog::settings() const
{
    return m_widget->settings();
}
