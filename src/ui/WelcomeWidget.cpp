#include "WelcomeWidget.h"

#include <QLabel>
#include <QVBoxLayout>

#include "BrandingWidget.h"

WelcomeWidget::WelcomeWidget(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setAlignment(Qt::AlignCenter);

    layout->addWidget(new BrandingWidget(this), 0, Qt::AlignHCenter);

    auto *subtitle = new QLabel(QStringLiteral("Use the Connection menu, or the Saved Connections panel, to open a session."), this);
    subtitle->setAlignment(Qt::AlignHCenter);
    subtitle->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->addWidget(subtitle);
}
