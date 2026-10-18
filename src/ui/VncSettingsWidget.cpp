#include "VncSettingsWidget.h"

#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>

VncSettingsWidget::VncSettingsWidget(QWidget *parent)
    : QWidget(parent)
    , m_hostEdit(new QLineEdit(this))
    , m_portSpin(new QSpinBox(this))
    , m_passwordEdit(new QLineEdit(this))
{
    m_hostEdit->setPlaceholderText(QStringLiteral("hostname or IP address"));
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(5900);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("blank = ask when the server wants one"));

    auto *form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(QStringLiteral("Host:"), m_hostEdit);
    form->addRow(QStringLiteral("Port:"), m_portSpin);
    form->addRow(QStringLiteral("Password:"), m_passwordEdit);
}

VncConnectionSettings VncSettingsWidget::settings() const
{
    VncConnectionSettings s;
    s.host = m_hostEdit->text().trimmed();
    s.port = static_cast<quint16>(m_portSpin->value());
    s.password = m_passwordEdit->text();
    return s;
}

void VncSettingsWidget::setSettings(const VncConnectionSettings &settings)
{
    m_hostEdit->setText(settings.host);
    m_portSpin->setValue(settings.port);
    m_passwordEdit->setText(settings.password);
}
