#include "VncTransport.h"

#include <QInputDialog>
#include <QLineEdit>

#include "VncClient.h"

VncTransport::VncTransport(VncConnectionSettings settings, QObject *parent)
    : Transport(parent)
    , m_settings(std::move(settings))
    , m_client(new VncClient(this))
{
    m_client->setPasswordProvider([this](QString &password) {
        bool ok = false;
        password = QInputDialog::getText(nullptr, QStringLiteral("VNC Password"),
                                         QStringLiteral("Password for %1:").arg(m_settings.host), QLineEdit::Password, QString(), &ok);
        return ok;
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
