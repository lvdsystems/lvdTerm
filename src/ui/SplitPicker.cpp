#include "SplitPicker.h"

#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/ProfileStore.h"

namespace
{
constexpr int kProfileIndexRole = Qt::UserRole;
}

SplitPicker::SplitPicker(ProfileStore *store, QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    auto *heading = new QLabel(QStringLiteral("New pane - connect to:"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    auto *duplicateButton = new QPushButton(QStringLiteral("Duplicate This Connection"), this);
    connect(duplicateButton, &QPushButton::clicked, this, &SplitPicker::duplicateRequested);
    layout->addWidget(duplicateButton);

    layout->addWidget(new QLabel(QStringLiteral("Saved Connections:"), this));
    auto *list = new QListWidget(this);
    const QVector<ConnectionProfile> &profiles = store->profiles();
    for (int i = 0; i < profiles.size(); ++i) {
        const ConnectionProfile &profile = profiles.at(i);
        const QString label = profile.folder.isEmpty() ? profile.name : QStringLiteral("%1/%2").arg(profile.folder, profile.name);
        auto *item = new QListWidgetItem(label, list);
        item->setData(kProfileIndexRole, i);
    }
    connect(list, &QListWidget::itemActivated, this, [this, store](QListWidgetItem *item) {
        const int i = item->data(kProfileIndexRole).toInt();
        if (i >= 0 && i < store->profiles().size())
            emit profileChosen(store->profiles().at(i));
    });
    layout->addWidget(list, 1);

    auto *newRow = new QHBoxLayout();
    auto *serialButton = new QPushButton(QStringLiteral("New Serial..."), this);
    auto *telnetButton = new QPushButton(QStringLiteral("New Telnet..."), this);
    auto *sshButton = new QPushButton(QStringLiteral("New SSH..."), this);
    connect(serialButton, &QPushButton::clicked, this, &SplitPicker::newSerialRequested);
    connect(telnetButton, &QPushButton::clicked, this, &SplitPicker::newTelnetRequested);
    connect(sshButton, &QPushButton::clicked, this, &SplitPicker::newSshRequested);
    newRow->addWidget(serialButton);
    newRow->addWidget(telnetButton);
    newRow->addWidget(sshButton);
    layout->addLayout(newRow);

    auto *cancelButton = new QPushButton(QStringLiteral("Cancel"), this);
    connect(cancelButton, &QPushButton::clicked, this, &SplitPicker::cancelled);
    layout->addWidget(cancelButton);
}

void SplitPicker::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        emit cancelled();
        return;
    }
    QWidget::keyPressEvent(event);
}
