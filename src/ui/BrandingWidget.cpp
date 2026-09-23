#include "BrandingWidget.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFont>
#include <QLabel>
#include <QPixmap>
#include <QUrl>
#include <QVBoxLayout>

#include "Version.h"

BrandingWidget::BrandingWidget(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(4);
    layout->setAlignment(Qt::AlignHCenter);

    // The source logo (LVD Systems S.r.l.'s own, see
    // src/resources/branding.qrc) has a dark gradient that reads fine on
    // the app's default Dark theme but loses contrast on Light - a plain
    // light card behind it keeps it legible either way without needing to
    // recolor the source image itself.
    auto *logoCard = new QWidget(this);
    logoCard->setStyleSheet(QStringLiteral("background-color: white; border-radius: 6px;"));
    auto *logoCardLayout = new QVBoxLayout(logoCard);
    logoCardLayout->setContentsMargins(16, 12, 16, 12);
    auto *logoLabel = new QLabel(logoCard);
    const QPixmap logo(QStringLiteral(":/branding/logo_lvd.png"));
    if (!logo.isNull())
        logoLabel->setPixmap(logo.scaledToWidth(260, Qt::SmoothTransformation));
    logoLabel->setAlignment(Qt::AlignHCenter);
    logoCardLayout->addWidget(logoLabel);
    layout->addWidget(logoCard, 0, Qt::AlignHCenter);

    auto *companyLabel = new QLabel(QStringLiteral("LVD Systems S.r.l."), this);
    QFont companyFont = companyLabel->font();
    companyFont.setPointSize(companyFont.pointSize() + 6);
    companyFont.setBold(true);
    companyLabel->setFont(companyFont);
    companyLabel->setAlignment(Qt::AlignHCenter);

    auto *appNameLabel = new QLabel(QStringLiteral("lvdterm"), this);
    QFont appNameFont = appNameLabel->font();
    appNameFont.setPointSize(appNameFont.pointSize() + 2);
    appNameLabel->setFont(appNameFont);
    appNameLabel->setAlignment(Qt::AlignHCenter);

    auto *versionLabel = new QLabel(QStringLiteral("Version %1").arg(QStringLiteral(LVDTERM_VERSION_STRING)), this);
    versionLabel->setAlignment(Qt::AlignHCenter);

    auto *linksLabel = new QLabel(this);
    linksLabel->setTextFormat(Qt::RichText);
    linksLabel->setText(QStringLiteral("<a href=\"https://www.lvdsystems.eu\">www.lvdsystems.eu</a> &nbsp;|&nbsp; "
                                        "<a href=\"https://www.lvdopen.eu\">www.lvdopen.eu</a>"));
    linksLabel->setOpenExternalLinks(true);
    linksLabel->setAlignment(Qt::AlignHCenter);

    layout->addWidget(companyLabel);
    layout->addWidget(appNameLabel);
    layout->addWidget(versionLabel);
    layout->addWidget(linksLabel);
}
