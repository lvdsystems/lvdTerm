#include "VncTransport.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "VncClient.h"
#include "core/AppSettings.h"
#include "ui/PromptParent.h"

namespace
{
// VeNCrypt *Plain sub-types: username and password, sent inside TLS.
bool askCredentials(const QString &host, QString &user, QString &password)
{
    QDialog dialog(promptParent());
    dialog.setWindowTitle(QStringLiteral("VNC Credentials"));
    auto *userEdit = new QLineEdit(user, &dialog);
    auto *passwordEdit = new QLineEdit(password, &dialog);
    passwordEdit->setEchoMode(QLineEdit::Password);

    auto *form = new QFormLayout();
    form->addRow(QStringLiteral("Username:"), userEdit);
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
    password = passwordEdit->text();
    return true;
}

// Trust on first use, like an SSH host key: an unknown or changed
// certificate is shown to the user, who can trust it for good, for this
// connection only, or refuse it.
bool checkCertificate(const QString &host, quint16 port, const QString &fingerprint, const QString &subject, const QString &issuer)
{
    const QString key = QStringLiteral("%1:%2").arg(host).arg(port);
    const QString known = AppSettings::instance().knownVncCertificate(key);
    if (!known.isEmpty() && known.compare(fingerprint, Qt::CaseInsensitive) == 0)
        return true;

    const QString details = QStringLiteral("Subject: %1\nIssuer: %2\nSHA-256 fingerprint:\n%3").arg(subject, issuer, fingerprint);
    QMessageBox box(promptParent());
    box.setWindowTitle(QStringLiteral("VNC Server Certificate"));
    if (known.isEmpty()) {
        box.setIcon(QMessageBox::Question);
        box.setText(QStringLiteral("The VNC server %1 presented a certificate lvdterm hasn't seen before.").arg(key));
        box.setInformativeText(QStringLiteral("Check the fingerprint with the server's administrator if unsure.\n\n") + details);
    } else {
        box.setIcon(QMessageBox::Warning);
        box.setText(QStringLiteral("WARNING: the certificate of VNC server %1 has CHANGED.").arg(key));
        box.setInformativeText(QStringLiteral("This can mean someone is intercepting the connection, or that the server's "
                                              "certificate was replaced.\n\nPreviously trusted fingerprint:\n%1\n\nNew certificate:\n%2")
                                   .arg(known, details));
    }
    QPushButton *always = box.addButton(QStringLiteral("Trust Always"), QMessageBox::AcceptRole);
    QPushButton *once = box.addButton(QStringLiteral("Trust Once"), QMessageBox::AcceptRole);
    QPushButton *reject = box.addButton(QStringLiteral("Reject"), QMessageBox::RejectRole);
    box.setDefaultButton(reject);
    box.setEscapeButton(reject);
    box.exec();

    if (box.clickedButton() == always) {
        AppSettings::instance().setKnownVncCertificate(key, fingerprint);
        return true;
    }
    return box.clickedButton() == once;
}
} // namespace

VncTransport::VncTransport(VncConnectionSettings settings, QObject *parent)
    : Transport(parent)
    , m_settings(std::move(settings))
    , m_client(new VncClient(this))
{
    m_client->setPasswordProvider([this](QString &password) {
        bool ok = false;
        password = QInputDialog::getText(promptParent(), QStringLiteral("VNC Password"),
                                         QStringLiteral("Password for %1:").arg(m_settings.host), QLineEdit::Password, QString(), &ok);
        return ok;
    });
    m_client->setCredentialsProvider([this](QString &user, QString &password) { return askCredentials(m_settings.host, user, password); });
    m_client->setCertificateChecker([this](const QString &fingerprint, const QString &subject, const QString &issuer) {
        return checkCertificate(m_settings.host, m_settings.port, fingerprint, subject, issuer);
    });

    connect(m_client, &VncClient::ready, this, [this] { setState(State::Connected); });
    connect(m_client, &VncClient::failed, this, [this](const QString &message) {
        emit errorOccurred(message);
        setState(State::Error);
    });
    connect(m_client, &VncClient::closed, this, [this] {
        if (state() != State::Error)
            setState(State::Disconnected);
    });
}

void VncTransport::connectToHost()
{
    setState(State::Connecting);
    m_client->connectToHost(m_settings.host, m_settings.port, m_settings.password);
}

void VncTransport::disconnectFromHost()
{
    m_client->close();
    setState(State::Disconnected);
}
