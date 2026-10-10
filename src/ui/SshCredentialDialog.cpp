#include "SshCredentialDialog.h"

#include <QInputDialog>
#include <QLineEdit>

namespace SshCredentialDialog
{

bool resolve(SshConnectionSettings &settings)
{
    if (settings.promptForUsername) {
        bool accepted = false;
        const QString value = QInputDialog::getText(nullptr, QStringLiteral("SSH Username"),
                                                      QStringLiteral("Username for %1:").arg(settings.host), QLineEdit::Normal, QString(),
                                                      &accepted);
        if (!accepted)
            return false;
        settings.username = value;
    }

    if (settings.authMethod == SshConnectionSettings::AuthMethod::Password && settings.promptForPassword) {
        bool accepted = false;
        const QString value = QInputDialog::getText(nullptr, QStringLiteral("SSH Password"),
                                                      QStringLiteral("Password for %1@%2:").arg(settings.username, settings.host),
                                                      QLineEdit::Password, QString(), &accepted);
        if (!accepted)
            return false;
        settings.password = value;
    }

    return true;
}

} // namespace SshCredentialDialog
