#include "RdpSettingsWidget.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>

RdpSettingsWidget::RdpSettingsWidget(QWidget *parent)
    : QWidget(parent)
    , m_hostEdit(new QLineEdit(this))
    , m_portSpin(new QSpinBox(this))
    , m_usernameEdit(new QLineEdit(this))
    , m_domainEdit(new QLineEdit(this))
    , m_passwordEdit(new QLineEdit(this))
    , m_widthSpin(new QSpinBox(this))
    , m_heightSpin(new QSpinBox(this))
{
    m_hostEdit->setPlaceholderText(QStringLiteral("hostname or IP address"));
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(3389);
    m_usernameEdit->setPlaceholderText(QStringLiteral("blank = ask when the server wants one"));
    m_domainEdit->setPlaceholderText(QStringLiteral("optional"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(QStringLiteral("blank = ask when the server wants one"));
    m_widthSpin->setRange(640, 8192);
    m_widthSpin->setValue(1280);
    m_heightSpin->setRange(480, 8192);
    m_heightSpin->setValue(800);

    auto *sizeRow = new QWidget(this);
    auto *sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->addWidget(m_widthSpin);
    sizeLayout->addWidget(new QLabel(QStringLiteral("x"), this));
    sizeLayout->addWidget(m_heightSpin);
    sizeLayout->addStretch();

    auto *form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(QStringLiteral("Host:"), m_hostEdit);
    form->addRow(QStringLiteral("Port:"), m_portSpin);
    form->addRow(QStringLiteral("Username:"), m_usernameEdit);
    form->addRow(QStringLiteral("Domain:"), m_domainEdit);
    form->addRow(QStringLiteral("Password:"), m_passwordEdit);
    form->addRow(QStringLiteral("Desktop size:"), sizeRow);
}

RdpConnectionSettings RdpSettingsWidget::settings() const
{
    RdpConnectionSettings s;
    s.host = m_hostEdit->text().trimmed();
    s.port = static_cast<quint16>(m_portSpin->value());
    s.username = m_usernameEdit->text().trimmed();
    s.domain = m_domainEdit->text().trimmed();
    s.password = m_passwordEdit->text();
    s.desktopWidth = m_widthSpin->value();
    s.desktopHeight = m_heightSpin->value();
    return s;
}

void RdpSettingsWidget::setSettings(const RdpConnectionSettings &settings)
{
    m_hostEdit->setText(settings.host);
    m_portSpin->setValue(settings.port);
    m_usernameEdit->setText(settings.username);
    m_domainEdit->setText(settings.domain);
    m_passwordEdit->setText(settings.password);
    m_widthSpin->setValue(settings.desktopWidth);
    m_heightSpin->setValue(settings.desktopHeight);
}
