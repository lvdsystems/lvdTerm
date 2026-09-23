#include "AboutDialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "BrandingWidget.h"

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("About lvdterm"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new BrandingWidget(this), 0, Qt::AlignHCenter);

    auto *description = new QLabel(QStringLiteral("A tabbed and splittable SSH/Telnet/serial terminal emulator for Windows."), this);
    description->setAlignment(Qt::AlignHCenter);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *licenseLabel = new QLabel(QStringLiteral("Licensed under the GNU General Public License, version 3 or later."), this);
    licenseLabel->setAlignment(Qt::AlignHCenter);
    licenseLabel->setWordWrap(true);
    layout->addWidget(licenseLabel);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *licenseButton = buttons->addButton(QStringLiteral("License..."), QDialogButtonBox::ActionRole);
    QPushButton *noticesButton = buttons->addButton(QStringLiteral("Third-Party Notices..."), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);

    connect(licenseButton, &QPushButton::clicked, this,
            [this] { showResourceText(QStringLiteral("License"), QStringLiteral(":/license/LICENSE")); });
    connect(noticesButton, &QPushButton::clicked, this,
            [this] { showResourceText(QStringLiteral("Third-Party Notices"), QStringLiteral(":/license/THIRD_PARTY_LICENSES.md")); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    layout->addWidget(buttons);
}

void AboutDialog::showResourceText(const QString &title, const QString &resourcePath)
{
    QFile file(resourcePath);
    const QString text = file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll())
                                                                           : QStringLiteral("Could not load %1.").arg(resourcePath);

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(title);
    dialog->resize(700, 600);
    dialog->setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(dialog);
    auto *textEdit = new QPlainTextEdit(dialog);
    textEdit->setReadOnly(true);
    textEdit->setPlainText(text);
    textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(textEdit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    layout->addWidget(buttons);

    dialog->show();
}
