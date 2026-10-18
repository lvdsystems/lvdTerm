#include "RdpTransport.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include "RdpClient.h"

namespace
{
// Asks for the credentials the server wants (GUI thread - RdpClient calls
// this on it, with the session thread waiting).
bool askCredentials(const QString &host, QString &user, QString &password, QString &domain)
{
    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("RDP Credentials"));
    auto *userEdit = new QLineEdit(user, &dialog);
    auto *domainEdit = new QLineEdit(domain, &dialog);
    auto *passwordEdit = new QLineEdit(password, &dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);

    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("Username:"), userEdit);
    form->addRow(QStringLiteral("Domain:"), domainEdit);
    form->addRow(QStringLiteral("Password:"), passwordEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QStringLiteral("Credentials for %1:").arg(host), &dialog));
    layout->addLayout(form);
    layout->addWidget(buttons);

    (user.isEmpty() ? userEdit : passwordEdit)->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return false;
    user = userEdit->text();
    domain = domainEdit->text();
    password = passwordEdit->text();
    return true;
}
} // namespace

RdpTransport::RdpTransport(RdpConnectionSettings settings, QObject *parent)
    : RdpTransport(std::make_shared<RdpConnectionSettings>(std::move(settings)), parent)
{
}

RdpTransport::RdpTransport(std::shared_ptr<RdpConnectionSettings> settings, QObject *parent)
    : Transport(parent)
    , m_settings(std::move(settings))
    , m_client(new RdpClient(this))
{
    m_client->setCredentialProvider([this](QString &user, QString &password, QString &domain) {
        if (!askCredentials(m_settings->host, user, password, domain))
            return false;
        // Remembered for this pane's later reconnects (not saved anywhere).
        m_settings->username = user;
        m_settings->password = password;
        m_settings->domain = domain;
        return true;
    });

    connect(m_client, &RdpClient::ready, this, [this] { setState(State::Connected); });
    connect(m_client, &RdpClient::failed, this, [this](const QString &message) {
        emit errorOccurred(message);
        setState(State::Error);
    });
    connect(m_client, &RdpClient::closed, this, [this] {
        if (state() != State::Error)
            setState(State::Disconnected);
    });
}

void RdpTransport::connectToHost()
{
    setState(State::Connecting);
    m_client->connectToHost(*m_settings);
}

void RdpTransport::disconnectFromHost()
{
    m_client->close();
    setState(State::Disconnected);
}
