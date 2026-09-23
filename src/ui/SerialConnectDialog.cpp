#include "SerialConnectDialog.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

#include "SerialSettingsWidget.h"

SerialConnectDialog::SerialConnectDialog(QWidget *parent) : QDialog(parent), m_widget(new SerialSettingsWidget(this))
{
    setWindowTitle(QStringLiteral("New Serial Connection"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_widget);
    layout->addWidget(buttons);
}

SerialPortSettings SerialConnectDialog::settings() const
{
    return m_widget->settings();
}
