#include "VncConnectDialog.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

#include "VncSettingsWidget.h"

VncConnectDialog::VncConnectDialog(QWidget *parent) : QDialog(parent), m_widget(new VncSettingsWidget(this))
{
    setWindowTitle(QStringLiteral("New VNC Connection"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_widget);
    layout->addWidget(buttons);
}

void VncConnectDialog::setSettings(const VncConnectionSettings &settings)
{
    m_widget->setSettings(settings);
}

VncConnectionSettings VncConnectDialog::settings() const
{
    return m_widget->settings();
}
